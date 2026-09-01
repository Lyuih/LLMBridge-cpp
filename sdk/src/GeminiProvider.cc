#include <sstream>
#include <json/json.h>
#include "../include/httplib.h"
#include "../include/GeminiProvider.h"
#include "../include/logger.h"
#include "../include/fields.h"

namespace chat_sdk
{
    namespace
    {
        // 把 ToolDefinition 转成 Gemini 的 functionDeclaration
        Json::Value toolToFunctionDeclaration(const ToolDefinition &def)
        {
            Json::Value decl;
            decl["name"] = def.name;
            if (!def.description.empty())
            {
                decl["description"] = def.description;
            }
            Json::Value params;
            params["type"] = "object";
            Json::Value props;
            Json::Value required(Json::arrayValue);
            for (const auto &p : def.parameters)
            {
                Json::Value prop;
                prop["type"] = p.type;
                if (!p.description.empty())
                {
                    prop["description"] = p.description;
                }
                props[p.name] = prop;
                if (p.required)
                {
                    required.append(p.name);
                }
            }
            if (!props.empty())
            {
                params["properties"] = props;
            }
            if (!required.empty())
            {
                params["required"] = required;
            }
            decl["parameters"] = params;
            return decl;
        }

        // 把 JSON 字符串参数转成 Json::Value(失败时包成 {"value": ...})
        Json::Value parseArgs(const std::string &args)
        {
            Json::Value value;
            if (args.empty())
            {
                value["value"] = "";
                return value;
            }
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream ss(args);
            if (Json::parseFromStream(builder, ss, &value, &errs))
            {
                return value;
            }
            value["value"] = args;
            return value;
        }
    }

    // 模型初始化
    bool GeminiProvider::initModel(const std::map<std::string, std::string> &model_config)
    {
        auto it = model_config.find("api_key");
        if (it == model_config.end())
        {
            LOG_ERROR("gemini-2.5-flash-lite获取api_key失败!");
            return false;
        }
        api_key_ = it->second;
        it = model_config.find("base_url");
        if (it == model_config.end())
        {
            LOG_ERROR("gemini-2.5-flash-lite获取base_url失败!");
            return false;
        }
        endPoint_ = it->second;
        // 可选: 配置自定义模型名
        it = model_config.find("model_name");
        if (it != model_config.end() && !it->second.empty())
        {
            model_name_ = it->second;
        }
        isAvailable_ = true;
        LOG_INFO("gemini-2.5-flash-lite模型初始化成功{}", endPoint_);
        return true;
    }

    // 发送消息给模型 全量返回(支持工具)
    LLMResponse GeminiProvider::sendMessage(const std::vector<Message> &messages,
                                            const std::map<std::string, std::string> &request_param,
                                            const std::vector<ToolDefinition> &tools)
    {
        LLMResponse resp;
        if (!isAvailable())
        {
            resp.error = "gemini模型不可用";
            LOG_ERROR("{}", resp.error);
            return resp;
        }
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
        std::string json_msg = buildRequestBody(messages, temperature, max_tokens, false, tools);

        httplib::Client client(endPoint_);
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        client.set_proxy("127.0.0.1", 7897);
        httplib::Headers headers = {
            {"x-goog-api-key", api_key_}};
        auto response = client.Post(
            "/v1beta/models/" + getModelName() + ":generateContent",
            headers,
            json_msg,
            "application/json");
        if (!response)
        {
            resp.error = "连接Gemini API失败,请检查网络和ssl";
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        if (response->status != 200)
        {
            resp.error = "Gemini API 返回非200状态 " + std::to_string(response->status) + " - " + response->body;
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        parseResponse(response->body, resp);
        return resp;
    }

    // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
    std::string GeminiProvider::sendMessageStream(const std::vector<Message> &messages,
                                                  const std::map<std::string, std::string> &request_param, func_stream callback)
    {
        using namespace json_fields;

        if (!isAvailable())
        {
            LOG_ERROR("gemini-2.5-flash-lite模型不可用");
            return "";
        }

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
        std::string json_msg = buildRequestBody(messages, temperature, max_tokens, true, {});

        httplib::Client client(endPoint_);
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        client.set_proxy("127.0.0.1", 7897);

        httplib::Headers headers = {
            {"x-goog-api-key", api_key_},
            {"Accept", "text/event-stream"},
            {"Content-Type", "application/json"}};

        std::string buffer;
        bool gotError = false;
        std::string errorMsg;
        int statusCode = 0;
        bool streamFinish = false;
        std::string full_content;

        httplib::Request req;
        req.method = "POST";
        req.path = "/v1beta/models/" + getModelName() + ":streamGenerateContent?alt=sse";
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
    std::string GeminiProvider::buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream,
                                                 const std::vector<ToolDefinition> &tools)
    {
        using namespace json_fields;
        Json::Value contents;
        for (const auto &message : messages)
        {
            Json::Value msg;
            // 角色映射: assistant -> model, system -> 跳过, tool -> function
            if (message.role == "assistant")
            {
                msg["role"] = "model";
            }
            else if (message.role == "tool")
            {
                msg["role"] = "function";
            }
            else if (message.role == "system")
            {
                continue;
            }
            else
            {
                msg["role"] = message.role;
            }

            Json::Value parts(Json::arrayValue);
            // 工具结果回填
            if (message.role == "tool")
            {
                Json::Value fr;
                std::string tool_name = message.tool_name.empty() ? message.tool_call_id : message.tool_name;
                fr["name"] = tool_name;
                Json::Value response;
                response["result"] = message.content.empty() ? " " : message.content;
                fr["response"] = response;
                Json::Value part;
                part["functionResponse"] = fr;
                parts.append(part);
            }
            // 助手请求工具调用
            else if (!message.tool_calls.empty())
            {
                for (const auto &tc : message.tool_calls)
                {
                    Json::Value fc;
                    fc["name"] = tc.name;
                    fc["args"] = parseArgs(tc.arguments);
                    Json::Value part;
                    part["functionCall"] = fc;
                    parts.append(part);
                }
            }
            else
            {
                Json::Value part;
                part["text"] = message.content.empty() ? " " : message.content;
                parts.append(part);
            }
            msg["parts"] = parts;
            contents.append(msg);
        }

        Json::Value request_body;
        request_body["contents"] = contents;

        // 注入工具 schema
        if (!tools.empty())
        {
            Json::Value decls(Json::arrayValue);
            for (const auto &def : tools)
            {
                decls.append(toolToFunctionDeclaration(def));
            }
            Json::Value tool;
            tool["functionDeclarations"] = decls;
            Json::Value tools_json(Json::arrayValue);
            tools_json.append(tool);
            request_body["tools"] = tools_json;
        }

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

    // 从响应 JSON 解析内容 / 工具调用 / token 用量
    void GeminiProvider::parseResponse(const std::string &response_body, LLMResponse &resp)
    {
        using namespace json_fields;

        Json::Value json;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(response_body);
        if (!Json::parseFromStream(readBuilder, json_stream, &json, &errs))
        {
            resp.error = "反序列化失败:" + errs;
            LOG_ERROR("{}", resp.error);
            return;
        }

        // token 用量
        if (json.isMember("usageMetadata") && json["usageMetadata"].isObject())
        {
            resp.input_tokens = json["usageMetadata"].get("promptTokenCount", 0).asInt();
            resp.output_tokens = json["usageMetadata"].get("candidatesTokenCount", 0).asInt();
        }

        if (!json.isMember("candidates") || !json["candidates"].isArray() || json["candidates"].empty())
        {
            resp.error = "响应缺少 candidates";
            LOG_WARN("{}", resp.error);
            return;
        }

        const Json::Value &candidate = json["candidates"][0];
        if (!candidate.isMember("content") || !candidate["content"].isMember("parts") ||
            !candidate["content"]["parts"].isArray())
        {
            resp.error = "响应缺少 content.parts";
            LOG_WARN("{}", resp.error);
            return;
        }

        int tool_index = 0;
        for (const auto &part : candidate["content"]["parts"])
        {
            if (part.isMember("text"))
            {
                resp.content += part["text"].asString();
            }
            else if (part.isMember("functionCall") && part["functionCall"].isObject())
            {
                ToolCall tc;
                const Json::Value &fc = part["functionCall"];
                tc.name = fc.get("name", "").asString();
                // args 为对象,序列化成 JSON 字符串
                if (fc.isMember("args") && fc["args"].isObject())
                {
                    Json::StreamWriterBuilder w;
                    tc.arguments = Json::writeString(w, fc["args"]);
                }
                // Gemini 无调用 id,合成一个
                tc.id = "call_gemini_" + tc.name + "_" + std::to_string(tool_index++);
                if (!tc.name.empty())
                {
                    resp.tool_calls.push_back(std::move(tc));
                }
            }
        }

        resp.success = true;
        LOG_INFO("Gemini响应解析成功 content:{} tool_calls:{}", resp.content, resp.tool_calls.size());
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
        if (json_str.compare(0, 6, "data: ") == 0)
        {
            json_str = json_str.substr(6);
        }
        else if (json_str.find("\"candidates\"") == std::string::npos)
        {
            return;
        }

        json_str.erase(0, json_str.find_first_not_of(" \r\n\t"));
        json_str.erase(json_str.find_last_not_of(" \r\n\t") + 1);

        if (json_str.empty())
        {
            return;
        }

        Json::Value chunk;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(json_str);
        if (!Json::parseFromStream(readBuilder, json_stream, &chunk, &errs))
        {
            LOG_ERROR("反序列化SSE事件失败:{}, 原始串:{}", errs, json_str);
            return;
        }

        if (chunk.isMember("candidates") &&
            chunk["candidates"].isArray() &&
            !chunk["candidates"].empty())
        {
            const Json::Value &candidate = chunk["candidates"][0];

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

        if (chunk.isMember("error"))
        {
            std::string error_msg = chunk["error"]["message"].asString();
            LOG_ERROR("Gemini API错误:{}", error_msg);
            callback(error_msg, true);
            streamFinish = true;
        }
    }
}
