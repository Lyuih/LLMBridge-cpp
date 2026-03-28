#include <sstream>
#include <json/json.h>
#include "../include/httplib.h"
#include "../include/DeepSeekProvider.h"
#include "../include/logger.h"
#include "../include/fields.h"

namespace chat_sdk
{
    // 模型初始化
    bool DeepSeekProvider::initModel(const std::map<std::string, std::string> &model_config)
    {
        // 1. 获取api_key
        auto it = model_config.find("api_key");
        if (it == model_config.end())
        {
            // 没有
            LOG_ERROR("deepseek获取api_key失败!");
            return false;
        }
        api_key_ = it->second;
        // 2.获取base url
        it = model_config.find("base_url");
        if (it == model_config.end())
        {
            // 没有
            LOG_ERROR("deepseek获取base_url失败!");
            return false;
        }
        endPoint_ = it->second;
        // 3.标记初始化成功
        isAvailable_ = true;
        LOG_INFO("成功 success");
        LOG_INFO("deepseek模型初始化成功{}", endPoint_);
        return true;
    }

    // 发送消息给模型
    // 全量返回
    std::string DeepSeekProvider::sendMessage(const std::vector<Message> &messages,
                                              const std::map<std::string, std::string> &request_param)
    {
        // 1.检查模型是否有效
        if (!isAvailable())
        {
            LOG_ERROR("deepseek模型失效");
            return "";
        }
        // 2.获取采用温度和最大tokens
        double temperature = 0.7;
        int max_tokens = 2048;
        if (request_param.find("temperature") != request_param.end())
        {
            temperature = std::stof(request_param.at("temperature"));
        }
        if (request_param.find("max_tokens") != request_param.end())
        {
            max_tokens = std::stoi(request_param.at("max_tokens"));
        }
        std::string json_msg = buildRequestBody(messages, temperature, max_tokens, false);

        // 创建http client
        httplib::Client client(endPoint_);
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        // 请求头
        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_key_},
            {"Content-Type", "application/json"}};
        // 发送post请求
        auto response = client.Post("/v1/chat/completions", headers, json_msg, "application/json");
        if (!response)
        {
            LOG_ERROR("连接deepseek API失败,请检查网络和ssl");
            return "";
        }
        LOG_DEBUG("deepseek API response status {}", response->status);
        LOG_DEBUG("deepseel API response body {}", response->body);

        // 检查响应
        if (response->status != 200)
        {
            LOG_ERROR("deepseek API 返回非200状态 {} - {}", response->status, response->body);
            return "";
        }
        return extractContentFromJson(response->body);
    }
    // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
    std::string DeepSeekProvider::sendMessageStream(const std::vector<Message> &messages,
                                                    const std::map<std::string, std::string> &request_param, func_stream callback)
    {
        LOG_DEBUG("流式响应");
        // 1.检查模型是否有效
        if (!isAvailable())
        {
            LOG_ERROR("deepseek模型失效");
            return "";
        }
        // 2.获取采用温度和最大tokens
        double temperature = 0.7;
        int max_tokens = 2048;
        if (request_param.find("temperature") != request_param.end())
        {
            temperature = std::stof(request_param.at("temperature"));
        }
        if (request_param.find("max_tokens") != request_param.end())
        {
            max_tokens = std::stoi(request_param.at("max_tokens"));
        }

        std::string json_msg = buildRequestBody(messages, temperature, max_tokens, true);

        // 创建http client
        httplib::Client client(endPoint_);
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        // 请求头
        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_key_},
            {"Accept", "text/event-stream"},
            {"Content-Type", "application/json"}};

        // 流式处理变量
        std::string buffer;
        bool gotError = false;
        std::string errorMsg;
        int statusCode = 0;
        bool streamFinish = false;
        std::string full_content;

        // 创建请求对象
        httplib::Request req;
        req.method = "POST";
        req.path = "/v1/chat/completions";
        req.headers = headers;
        req.body = json_msg;

        req.response_handler = [&](const httplib::Response &response)
        {
            statusCode = response.status;
            if (200 != statusCode)
            {
                gotError = true;
                errorMsg = "HTTP Error:" + std::to_string(statusCode);
                return false;
            }
            return true;
        };
        req.content_receiver = [&](const char *data, size_t len, uint64_t offset, uint64_t totalLength)
        {
            if (gotError)
            {
                return false;
            }
            buffer.append(data, len);
            // std::cout << "buffer:" << buffer << std::endl;
            LOG_INFO("buffer:{}", buffer);
            size_t pos = 0;
            while ((pos = buffer.find("\n\n")) != std::string::npos)
            {
                std::string event = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);

                processSseEvent(event, full_content, streamFinish, callback);
            }
            return true;
        };
        auto res = client.send(req);
        if (!res)
        {
            auto err = res.error();
            if (err == httplib::Error::Canceled && gotError)
            {
                LOG_ERROR("服务端报错被拦截: {}", errorMsg);
            }
            else
            {
                LOG_ERROR("网络错误:{}", std::to_string((int)err));
            }
            return "";
        }
        if (!streamFinish)
        {
            LOG_WARN("流式最终处理错误");
            callback("", true);
        }
        return full_content;
    }

    // 负责将 Message 列表和参数转为 JSON 字符串
    std::string DeepSeekProvider::buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream)
    {
        using namespace json_fields;
        // 构建历史信息
        Json::Value msg_array;
        for (const auto &message : messages)
        {
            Json::Value msg;
            msg[ROLE] = message.role;
            msg[CONTENT] = message.content;
            msg_array.append(msg);
        }
        Json::Value request_body;
        request_body[MODEL] = getModelName();
        request_body[MESSAGES] = msg_array;
        request_body[TEMPERATURE] = temp;
        request_body[MAX_TOKENS] = max_tokens;
        request_body[STREAM] = stream;
        // 序列化
        Json::StreamWriterBuilder writer;
        std::string json_string = Json::writeString(writer, request_body);
        LOG_DEBUG("deepseek请求数据序列化成功:{}", json_string);
        return json_string;
    }
    // 负责从返回的 JSON 中提取文本内容
    std::string DeepSeekProvider::extractContentFromJson(const std::string &response_body)
    {
        using namespace json_fields;

        // 反序列化
        Json::Value response_json;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(response_body);
        if (!Json::parseFromStream(readBuilder, json_stream, &response_json, &errs))
        {
            LOG_ERROR("反序列化失败:{}", errs);
            return "";
        }
        if (response_json.isMember(CHOICES) &&
            response_json[CHOICES].isArray() &&
            !response_json[CHOICES].empty())
        {
            int sz = response_json[CHOICES].size();
            std::string reply_content;
            for (int i = 0; i < sz; ++i)
            {
                auto &choice = response_json[CHOICES][i];
                if (choice.isMember(MESSAGE) &&
                    choice[MESSAGE].isMember(CONTENT))
                {
                    reply_content += choice[MESSAGE][CONTENT].asString();
                }
            }
            LOG_INFO("deepseek提出文本内容成功:{}", reply_content);
            return reply_content;
        }
        LOG_WARN("无法获取deepseek文本内容");
        return "null";
    }
    // 处理 SSE 事件流的单行解析
    void DeepSeekProvider::processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback)
    {
        using namespace json_fields;

        if (event.empty() || event[0] == ':')
        {
            return;
        }
        if (event.compare(0, 6, "data: ") != 0)
        {
            return;
        }
        std::string json_str = event.substr(6);
        if (json_str == "[DONE]")
        {
            callback("", true);
            streamFinish = true;
            return;
        }
        Json::Value chunk;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(json_str);
        if (Json::parseFromStream(readBuilder, json_stream, &chunk, &errs))
        {
            if (chunk.isMember(CHOICES) &&
                chunk[CHOICES].isArray() &&
                !chunk[CHOICES].empty() &&
                chunk[CHOICES][0].isMember(DELTA) &&
                chunk[CHOICES][0][DELTA].isMember(CONTENT))
            {
                std::string content = chunk[CHOICES][0][DELTA][CONTENT].asString();
                full_content += content;
                callback(content, false);
            }
        }
        else
        {
            LOG_ERROR("反序列化失败:{}", errs);
            return;
        }
    }
}