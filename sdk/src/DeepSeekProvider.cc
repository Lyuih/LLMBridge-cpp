#include <sstream>
#include <json/json.h>
#include "../include/httplib.h"
#include "../include/DeepSeekProvider.h"
#include "../include/logger.h"

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

        // 3.构建历史信息
        Json::Value msg_array;
        for (const auto &message : messages)
        {
            Json::Value msg;
            msg["role"] = message.role;
            msg["content"] = message.content;
            msg_array.append(msg);
        }
        // 4.构建请求体
        Json::Value request_body;
        request_body["model"] = "deepseek-chat";
        request_body["messages"] = msg_array;
        request_body["temperature"] = temperature;
        request_body["max_tokens"] = max_tokens;
        // 序列化
        Json::StreamWriterBuilder swb;
        std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());
        std::stringstream ss;
        int ret = sw->write(request_body, &ss);
        if (ret < 0)
        {
            LOG_ERROR("序列化失败");
            return "";
        }
        std::string json_msg = ss.str();
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
        // 解析响应体
        Json::Value response_json;
        Json::CharReaderBuilder crb;
        std::unique_ptr<Json::CharReader> cr(crb.newCharReader());
        bool flag = cr->parse(response->body.c_str(), response->body.c_str() + response->body.size(), &response_json, nullptr);
        if (!flag)
        {
            LOG_ERROR("反序列化失败");
            return "";
        }

        // 解析回复内容
        if (response_json.isMember("choices") &&
            response_json["choices"].isArray() &&
            !response_json["choices"].empty())
        {
            int sz = response_json.size();
            std::string reply_content;
            for (int i = 0; i < sz; ++i)
            {
                auto &choice = response_json["choices"][i];
                if (choice.isMember("message") &&
                    choice["message"].isMember("content"))
                {
                    reply_content += choice["message"]["content"].asString();
                }
            }
            LOG_INFO("deepseek :{}", reply_content);
            return reply_content;
        }
        LOG_ERROR("无法获取deepseek返回的结果");
        return "null";
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

        // 3.构建历史信息
        Json::Value msg_array;
        for (const auto &message : messages)
        {
            Json::Value msg;
            msg["role"] = message.role;
            msg["content"] = message.content;
            msg_array.append(msg);
        }
        // 4.构建请求体
        Json::Value request_body;
        request_body["model"] = "deepseek-chat";
        request_body["messages"] = msg_array;
        request_body["temperature"] = temperature;
        request_body["max_tokens"] = max_tokens;
        request_body["stream"] = true;
        // 序列化
        Json::StreamWriterBuilder swb;
        std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter());
        std::stringstream ss;
        int ret = sw->write(request_body, &ss);
        if (ret < 0)
        {
            LOG_ERROR("序列化失败");
            return "";
        }
        std::string json_msg = ss.str();

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
        std::string fullResponse;

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
            std::cout << "buffer:" << buffer << std::endl;
            size_t pos = 0;
            while ((pos = buffer.find("\n\n")) != std::string::npos)
            {
                std::string event = buffer.substr(0, pos);
                buffer.erase(0, pos + 2);
                if (event.empty() || event[0] == ':')
                {
                    continue;
                }
                if (event.compare(0, 6, "data: ") == 0)
                {
                    std::string json_str = event.substr(6);
                    if (json_str == "[DONE]")
                    {
                        callback("", true);
                        streamFinish = true;
                        return true;
                    }
                    Json::Value chunk;
                    Json::CharReaderBuilder readBuilder;
                    std::string errs;
                    std::istringstream json_stream(json_str);
                    if (Json::parseFromStream(readBuilder, json_stream, &chunk, &errs))
                    {
                        if (chunk.isMember("choices") &&
                            chunk["choices"].isArray() &&
                            !chunk["choices"].empty() &&
                            chunk["choices"][0].isMember("delta") &&
                            chunk["choices"][0]["delta"].isMember("content"))
                        {
                            std::string content = chunk["choices"][0]["delta"]["content"].asString();
                            fullResponse += content;
                            callback(content, false);
                        }
                    }
                }
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
        return fullResponse;
    }
}