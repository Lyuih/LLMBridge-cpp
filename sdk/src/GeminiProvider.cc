#include <sstream>
#include <json/json.h>
#include "../include/httplib.h"
#include "../include/GeminiProvider.h"
#include "../include/logger.h"
#include "../include/fields.h"

namespace chat_sdk
{

    // 模型初始化
    bool GeminiProvider::initModel(const std::map<std::string, std::string> &model_config)
    {
        // 1. 获取api_key
        auto it = model_config.find("api_key");
        if (it == model_config.end())
        {
            // 没有
            LOG_ERROR("gemini-2.5-flash-lite获取api_key失败!");
            return false;
        }
        api_key_ = it->second;
        // 2.获取base url
        it = model_config.find("base_url");
        if (it == model_config.end())
        {
            // 没有
            LOG_ERROR("gemini-2.5-flash-lite获取base_url失败!");
            return false;
        }
        endPoint_ = it->second;
        // 3.标记初始化成功
        isAvailable_ = true;
        LOG_INFO("成功 success");
        LOG_INFO("gemini-2.5-flash-lite模型初始化成功{}", endPoint_);
        return true;
    }

    // 发送消息给模型
    std::string GeminiProvider::sendMessage(const std::vector<Message> &messages,
                                            const std::map<std::string, std::string> &request_param)
    {
        // 1.检查模型是否有效

        if (!isAvailable())
        {
            LOG_ERROR("gemini-2.5-flash-lite模型不可用");
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
        client.set_proxy("127.0.0.1", 7897);
        // 请求头
        httplib::Headers headers = {
            {"x-goog-api-key", api_key_}};
        // 发送post请求
        auto response = client.Post(
            "/v1beta/models/gemini-2.5-flash-lite:generateContent",
            headers,
            json_msg,
            "application/json");
        if (!response)
        {
            LOG_ERROR("连接Gemini API失败,请检查网络和ssl");
            return "";
        }
        LOG_DEBUG("Gemini API response status {}", response->status);
        LOG_DEBUG("Gemini API response body {}", response->body);

        // 检查响应
        if (response->status != 200)
        {
            LOG_ERROR("Gemini API 返回非200状态 {} - {}", response->status, response->body);
            return "";
        }
        return extractContentFromJson(response->body);
    }
    // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
    std::string GeminiProvider::sendMessageStream(const std::vector<Message> &messages,
                                                  const std::map<std::string, std::string> &request_param, func_stream callback)
    {
        using namespace json_fields;

        // 1. 检查模型可用性
        if (!isAvailable())
        {
            LOG_ERROR("gemini-2.5-flash-lite模型不可用");
            return "";
        }

        // 2. 获取请求参数
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

        // 3. 创建HTTP客户端并配置
        httplib::Client client(endPoint_);
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        client.set_proxy("127.0.0.1", 7897);

        // 4. 配置请求头（修复：使用x-goog-api-key代替Authorization Bearer）
        httplib::Headers headers = {
            {"x-goog-api-key", api_key_},
            {"Accept", "text/event-stream"},
            {"Content-Type", "application/json"}};

        // 5. 流式处理变量
        std::string buffer;
        bool gotError = false;
        std::string errorMsg;
        int statusCode = 0;
        bool streamFinish = false;
        std::string full_content;

        // 6. 创建请求对象（修复：使用正确的流式端点）
        httplib::Request req;
        req.method = "POST";
        req.path = "/v1beta/models/" + getModelName() + ":streamGenerateContent?alt=sse"; // 原生Gemini流式端点
        // 如果需要使用OpenAI兼容模式，请注释上面一行并取消注释下面一行
        // req.path = "/v1beta/openai/chat/completions";
        req.headers = headers;
        req.body = json_msg;

        // 7. 响应处理器（处理HTTP状态码）
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

        // 8. 内容接收器（处理流式数据）
        req.content_receiver = [&](const char *data, size_t len, uint64_t offset, uint64_t totalLength)
        {
            if (gotError)
            {
                return false;
            }
            buffer.append(data, len);

            while (true)
            {
                size_t pos_rn = buffer.find("\r\n\r\n");
                size_t pos_n = buffer.find("\n\n");
                size_t pos = std::string::npos;
                size_t erase_len = 0;

                if (pos_rn != std::string::npos && (pos_n == std::string::npos || pos_rn < pos_n))
                {
                    pos = pos_rn;
                    erase_len = 4;
                }
                else if (pos_n != std::string::npos)
                {
                    pos = pos_n;
                    erase_len = 2;
                }
                else
                {
                    break;
                }

                std::string event = buffer.substr(0, pos);
                buffer.erase(0, pos + erase_len);

                processSseEvent(event, full_content, streamFinish, callback);

                if (streamFinish)
                {
                    break;
                }
            }
            return true;
        };

        // 9. 发送请求并处理结果
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
    std::string GeminiProvider::buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream)
    {
        using namespace json_fields;
        Json::Value contents; // Gemini 对话主体
        for (const auto &message : messages)
        {
            Json::Value msg;
            // 1. 角色：user / model（Gemini 固定格式）
            if (message.role == "assistant")
            {
                msg["role"] = "model";
            }
            else if (message.role == "system")
            {
                // Gemini 的 system 指令不在 contents 里设置，直接跳过或者记录
                continue;
            }
            else
            {
                msg["role"] = message.role;
            }

            // 2. 内容：必须包裹在 parts -> text 中（官方强制格式）
            Json::Value part;
            // 避免空字符串报错，给个空格垫底
            part["text"] = message.content.empty() ? " " : message.content;
            Json::Value parts;
            parts.append(part);
            msg["parts"] = parts;

            contents.append(msg);
        }

        Json::Value request_body;
        request_body["contents"] = contents;

        Json::Value system_instruction;
        Json::Value system_part;
        system_part["text"] = "你是一个友好、专业的AI助手，回答简洁明了";
        Json::Value system_parts;
        system_parts.append(system_part);
        system_instruction["parts"] = system_parts;
        request_body["systemInstruction"] = system_instruction;

        Json::Value generation_config;
        generation_config["temperature"] = temp;
        generation_config["maxOutputTokens"] = max_tokens;
        request_body["generationConfig"] = generation_config;

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::string json_string = Json::writeString(writer, request_body);
        LOG_DEBUG("gemini请求数据序列化成功:{}", json_string);
        return json_string;
    }

    // 负责从返回的 JSON 中提取文本内容
    std::string GeminiProvider::extractContentFromJson(const std::string &response_body)
    {
        using namespace json_fields;

        Json::Value response_json;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(response_body);
        if (!Json::parseFromStream(readBuilder, json_stream, &response_json, &errs))
        {
            LOG_ERROR("反序列化失败:{}", errs);
            return "";
        }

        // 1. 检查 candidates 数组是否存在且不为空
        if (response_json.isMember("candidates") &&
            response_json["candidates"].isArray() &&
            !response_json["candidates"].empty())
        {
            // 2. 获取第一个候选结果（Gemini 默认只返回一个）
            const Json::Value &candidate = response_json["candidates"][0];

            // 3. 检查 content 和 parts 字段是否存在
            if (candidate.isMember("content") &&
                candidate["content"].isMember("parts") &&
                candidate["content"]["parts"].isArray() &&
                !candidate["content"]["parts"].empty())
            {
                // 4. 提取 text 内容
                std::string reply_content = candidate["content"]["parts"][0]["text"].asString();
                LOG_INFO("Gemini提取文本内容成功:{}", reply_content);
                return reply_content;
            }
        }

        // 错误日志（保留原有逻辑）
        LOG_WARN("无法获取Gemini文本内容");
        return "null";
    }

    // 处理 SSE 事件流的单行解析
    void GeminiProvider::processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback)
    {
        using namespace json_fields;

        if (event.empty())
        {
            return;
        }

        std::string json_str = event;
        // 兼容带 "data: " 前缀的 sse 格式
        if (json_str.compare(0, 6, "data: ") == 0)
        {
            json_str = json_str.substr(6);
        }
        else if (json_str.find("\"candidates\"") == std::string::npos)
        {
            // 有时候返回头部行不需要处理
            return;
        }

        // 去除可能的首尾空白字符串
        json_str.erase(0, json_str.find_first_not_of(" \r\n\t"));
        json_str.erase(json_str.find_last_not_of(" \r\n\t") + 1);

        if (json_str.empty())
            return;

        // 反序列化JSON数据
        Json::Value chunk;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(json_str);
        if (!Json::parseFromStream(readBuilder, json_stream, &chunk, &errs))
        {
            LOG_ERROR("反序列化SSE事件失败:{}, 原始串:{}", errs, json_str);
            return;
        }

        // 解析Gemini原生格式响应
        if (chunk.isMember("candidates") &&
            chunk["candidates"].isArray() &&
            !chunk["candidates"].empty())
        {
            const Json::Value &candidate = chunk["candidates"][0];

            // 提取文本内容
            if (candidate.isMember("content") &&
                candidate["content"].isMember("parts") &&
                candidate["content"]["parts"].isArray() &&
                !candidate["content"]["parts"].empty())
            {
                std::string content = candidate["content"]["parts"][0]["text"].asString();
                if (!content.empty())
                {
                    full_content += content;
                    callback(content, false);
                    LOG_DEBUG("Gemini流式接收内容:{}", content);
                }
            }

            // 官方流结束标志
            if (candidate.isMember("finishReason"))
            {
                std::string finish_reason = candidate["finishReason"].asString();
                if (finish_reason == "STOP" || finish_reason == "SAFETY" || finish_reason == "MAX_TOKENS" || finish_reason != "")
                {
                    LOG_DEBUG("Gemini流式结束，原因:{}", finish_reason);
                    callback("", true);
                    streamFinish = true;
                    return;
                }
            }
        }

        // 错误处理
        if (chunk.isMember("error"))
        {
            std::string error_msg = chunk["error"]["message"].asString();
            LOG_ERROR("Gemini API错误:{}", error_msg);
            callback(error_msg, true);
            streamFinish = true;
        }
    }
}
