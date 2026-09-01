#include <sstream>
#include <json/json.h>
#include "../include/httplib.h"
#include "../include/OllamaLLMProvider.h"
#include "../include/logger.h"
#include "../include/fields.h"

namespace chat_sdk
{
    // 模型初始化
    bool OllamaLLMProvider::initModel(const std::map<std::string, std::string> &model_config)
    {
        if (model_config.find("model_name") == model_config.end())
        {
            LOG_ERROR("model_name");
            return false;
        }
        model_name_ = model_config.at("model_name");
        if (model_config.find("model_desc") == model_config.end())
        {
            LOG_WARN("model_desc");
            return false;
        }
        model_desc_ = model_config.at("model_desc");
        if (model_config.find("base_url") == model_config.end())
        {
            LOG_ERROR("base_url");
            return false;
        }
        endPoint_ = model_config.at("base_url");
        isAvailable_ = true;
        return true;
    }

    // 发送消息给模型 全量返回(支持工具)
    LLMResponse OllamaLLMProvider::sendMessage(const std::vector<Message> &messages,
                                               const std::map<std::string, std::string> &request_param,
                                               const std::vector<ToolDefinition> &tools)
    {
        LLMResponse resp;
        if (!isAvailable())
        {
            resp.error = "ollama " + model_name_ + " 模型未启动";
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        // 构建请求参数
        double temperature = 0.7f;
        int max_tokens = 1024;
        if (request_param.find("temperature") != request_param.end())
        {
            temperature = std::stof(request_param.at("temperature"));
        }
        if (request_param.find("max_tokens") != request_param.end())
        {
            max_tokens = std::stoi(request_param.at("max_tokens"));
        }
        std::string json_msg = buildRequestBody(messages, temperature, max_tokens, false, tools);

        httplib::Client client(endPoint_.c_str());
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        // 设置请求头
        httplib::Headers headers = {
            {"Content-Type", "application/json"}};

        auto response = client.Post("/api/chat", headers, json_msg, "application/json");
        if (!response)
        {
            resp.error = "连接ollama失败,检查ollama是否运行";
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        LOG_DEBUG("ollama API response status {}", response->status);
        LOG_DEBUG("ollama API response body {}", response->body);

        // 检查响应
        if (response->status != 200)
        {
            resp.error = "ollama返回非200状态 " + std::to_string(response->status) + " - " + response->body;
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        parseResponse(response->body, resp);
        return resp;
    }

    // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
    std::string OllamaLLMProvider::sendMessageStream(const std::vector<Message> &messages,
                                                     const std::map<std::string, std::string> &request_param, func_stream callback)
    {
        if (!isAvailable())
        {
            LOG_ERROR("ollama {} 模型未启动", model_name_);
            return "";
        }
        // 构建请求参数
        double temperature = 0.7f;
        int max_tokens = 1024;
        if (request_param.find("temperature") != request_param.end())
        {
            temperature = std::stof(request_param.at("temperature"));
        }
        if (request_param.find("max_tokens") != request_param.end())
        {
            max_tokens = std::stoi(request_param.at("max_tokens"));
        }
        std::string json_msg = buildRequestBody(messages, temperature, max_tokens, true, {});

        httplib::Client client(endPoint_.c_str());
        client.set_connection_timeout(30, 0);
        client.set_read_timeout(60, 0);
        // 设置请求头
        httplib::Headers headers = {
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
        req.path = "/api/chat";
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
            size_t pos = 0;
            while ((pos = buffer.find("\n")) != std::string::npos)
            {
                std::string event = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

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
    std::string OllamaLLMProvider::buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream,
                                                    const std::vector<ToolDefinition> &tools)
    {
        using namespace json_fields;
        // 构建历史信息
        Json::Value msg_array;
        for (const auto &message : messages)
        {
            Json::Value msg;
            msg[ROLE] = message.role;
            msg[CONTENT] = message.content;
            // 助手请求工具时携带 tool_calls
            if (!message.tool_calls.empty())
            {
                Json::Value calls(Json::arrayValue);
                for (const auto &tc : message.tool_calls)
                {
                    Json::Value call;
                    Json::Value fn;
                    fn["name"] = tc.name;
                    // arguments 需要是对象
                    Json::Value args;
                    Json::CharReaderBuilder builder;
                    std::string errs;
                    std::istringstream ss(tc.arguments);
                    if (Json::parseFromStream(builder, ss, &args, &errs))
                    {
                        fn["arguments"] = args;
                    }
                    else
                    {
                        fn["arguments"]["value"] = tc.arguments;
                    }
                    call["function"] = fn;
                    calls.append(call);
                }
                msg["tool_calls"] = calls;
            }
            msg_array.append(msg);
        }
        Json::Value request_body;
        request_body[MODEL] = getModelName();
        request_body[MESSAGES] = msg_array;
        Json::Value options;
        options[TEMPERATURE] = temp;
        options["num_ctx"] = max_tokens;
        request_body["options"] = options;
        request_body[STREAM] = stream;
        // 注入工具 schema(Ollama 用 OpenAI 兼容格式)
        if (!tools.empty())
        {
            Json::Value tools_json(Json::arrayValue);
            for (const auto &def : tools)
            {
                Json::Value t;
                t["type"] = "function";
                Json::Value fn;
                fn["name"] = def.name;
                if (!def.description.empty())
                {
                    fn["description"] = def.description;
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
                fn["parameters"] = params;
                t["function"] = fn;
                tools_json.append(t);
            }
            request_body["tools"] = tools_json;
        }
        // 序列化
        Json::StreamWriterBuilder writer;
        std::string json_string = Json::writeString(writer, request_body);
        LOG_DEBUG("ollama请求数据序列化成功:{}", json_string);
        return json_string;
    }

    // 从响应 JSON 解析内容 / 工具调用 / token 用量
    void OllamaLLMProvider::parseResponse(const std::string &response_body, LLMResponse &resp)
    {
        using namespace json_fields;
        /*
        {
            "model":"deepseek-r1:1.5b",
            "message":{"role":"assistant","content":"..."},
            "prompt_eval_count":2,
            "eval_count":181,
            "done":true
        }
        */
        // 反序列化
        Json::Value response_json;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(response_body);
        if (!Json::parseFromStream(readBuilder, json_stream, &response_json, &errs))
        {
            resp.error = "反序列化失败:" + errs;
            LOG_ERROR("{}", resp.error);
            return;
        }

        // token 用量
        resp.input_tokens = response_json.get("prompt_eval_count", 0).asInt();
        resp.output_tokens = response_json.get("eval_count", 0).asInt();

        if (!response_json.isMember("message") || !response_json["message"].isObject())
        {
            resp.error = "响应缺少 message";
            LOG_WARN("{}", resp.error);
            return;
        }

        const Json::Value &message = response_json["message"];
        resp.content = message.get(CONTENT, "").asString();

        // 解析工具调用(Ollama 与 OpenAI 兼容格式)
        if (message.isMember("tool_calls") && message["tool_calls"].isArray())
        {
            int idx = 0;
            for (const auto &call : message["tool_calls"])
            {
                ToolCall tc;
                tc.id = "call_ollama_" + std::to_string(idx++);
                if (call.isMember("function") && call["function"].isObject())
                {
                    tc.name = call["function"].get("name", "").asString();
                    if (call["function"].isMember("arguments") && call["function"]["arguments"].isObject())
                    {
                        Json::StreamWriterBuilder w;
                        tc.arguments = Json::writeString(w, call["function"]["arguments"]);
                    }
                }
                if (!tc.name.empty())
                {
                    resp.tool_calls.push_back(std::move(tc));
                }
            }
        }

        resp.success = true;
        LOG_INFO("ollama响应解析成功 content:{} tool_calls:{}", resp.content, resp.tool_calls.size());
    }

    // 处理 SSE 事件流的单行解析
    void OllamaLLMProvider::processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback)
    {
        using namespace json_fields;

        if (event.empty())
        {
            return;
        }
        std::string json_str = event.substr(0);

        Json::Value chunk;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(json_str);
        if (Json::parseFromStream(readBuilder, json_stream, &chunk, &errs))
        {
            if (chunk["done"].asBool())
            {
                streamFinish = true;
                callback("", true);
                return;
            }

            if (chunk.isMember("message") &&
                chunk["message"].isMember("content"))
            {
                std::string delta = chunk["message"]["content"].asString();
                full_content += delta;
                callback(delta, false);
            }
        }
        else
        {
            LOG_ERROR("反序列化失败:{}", errs);
            return;
        }
    }
}
