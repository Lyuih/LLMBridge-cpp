#include <sstream>
#include <json/json.h>
#include "../include/httplib.h"
#include "../include/GPTProvider.h"
#include "../include/logger.h"
#include "../include/fields.h"

namespace chat_sdk
{
    namespace
    {
        // 把 ToolDefinition 转成 OpenAI 格式的 function 对象
        Json::Value toolToFunction(const ToolDefinition &def)
        {
            Json::Value func;
            func["name"] = def.name;
            if (!def.description.empty())
            {
                func["description"] = def.description;
            }
            // parameters: JSON Schema 子集
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
            func["parameters"] = params;
            return func;
        }
    }

    // 模型初始化
    bool GPTProvider::initModel(const std::map<std::string, std::string> &model_config)
    {
        // 1. 获取api_key
        auto it = model_config.find("api_key");
        if (it == model_config.end() || it->second.empty())
        {
            LOG_ERROR("gpt获取api_key失败!");
            return false;
        }
        api_key_ = it->second;
        // 2. 获取base url
        it = model_config.find("base_url");
        if (it == model_config.end() || it->second.empty())
        {
            LOG_ERROR("gpt获取base_url失败!");
            return false;
        }
        endPoint_ = it->second;
        // 3. 可选: 配置自定义模型名(默认 chat-4o-mini)
        it = model_config.find("model_name");
        if (it != model_config.end() && !it->second.empty())
        {
            model_name_ = it->second;
        }
        // 4. 可选: 模型描述
        it = model_config.find("model_desc");
        if (it != model_config.end() && !it->second.empty())
        {
            model_desc_ = it->second;
        }
        // 5. 标记初始化成功
        isAvailable_ = true;
        LOG_INFO("gpt模型初始化成功{} model:{}", endPoint_, model_name_);
        return true;
    }

    // 发送消息给模型 全量返回(支持工具)
    LLMResponse GPTProvider::sendMessage(const std::vector<Message> &messages,
                                         const std::map<std::string, std::string> &request_param,
                                         const std::vector<ToolDefinition> &tools)
    {
        LLMResponse resp;
        if (!isAvailable())
        {
            resp.error = "gpt模型失效";
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
        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_key_},
            {"Content-Type", "application/json"}};
        auto response = client.Post("/v1/chat/completions", headers, json_msg, "application/json");
        if (!response)
        {
            resp.error = "连接OpenAI兼容API失败,请检查网络和ssl";
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        if (response->status != 200)
        {
            resp.error = "gpt API 返回非200状态 " + std::to_string(response->status) + " - " + response->body;
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        parseResponse(response->body, resp);
        return resp;
    }

    // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
    std::string GPTProvider::sendMessageStream(const std::vector<Message> &messages,
                                               const std::map<std::string, std::string> &request_param, func_stream callback)
    {
        LOG_DEBUG("gpt流式响应");
        if (!isAvailable())
        {
            LOG_ERROR("gpt模型失效");
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
        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_key_},
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
    std::string GPTProvider::buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream,
                                              const std::vector<ToolDefinition> &tools)
    {
        using namespace json_fields;
        Json::Value msg_array;
        for (const auto &message : messages)
        {
            Json::Value msg;
            msg[ROLE] = message.role;
            // tool 角色消息: 关联 tool_call_id
            if (message.role == "tool")
            {
                msg["tool_call_id"] = message.tool_call_id;
            }
            msg[CONTENT] = message.content.empty() ? "" : message.content;
            // assistant 请求工具时携带 tool_calls
            if (!message.tool_calls.empty())
            {
                Json::Value calls(Json::arrayValue);
                for (const auto &tc : message.tool_calls)
                {
                    Json::Value call;
                    call["id"] = tc.id;
                    call["type"] = "function";
                    Json::Value fn;
                    fn["name"] = tc.name;
                    fn["arguments"] = tc.arguments;
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
        request_body[TEMPERATURE] = temp;
        request_body[MAX_TOKENS] = max_tokens;
        request_body[STREAM] = stream;
        // 注入工具 schema
        if (!tools.empty())
        {
            Json::Value tools_json(Json::arrayValue);
            for (const auto &def : tools)
            {
                Json::Value t;
                t["type"] = "function";
                t["function"] = toolToFunction(def);
                tools_json.append(t);
            }
            request_body["tools"] = tools_json;
        }
        Json::StreamWriterBuilder writer;
        std::string json_string = Json::writeString(writer, request_body);
        LOG_DEBUG("gpt请求数据序列化成功:{}", json_string);
        return json_string;
    }

    // 从响应 JSON 解析内容 / 工具调用 / token 用量
    void GPTProvider::parseResponse(const std::string &response_body, LLMResponse &resp)
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
        if (json.isMember("usage") && json["usage"].isObject())
        {
            resp.input_tokens = json["usage"].get("prompt_tokens", 0).asInt();
            resp.output_tokens = json["usage"].get("completion_tokens", 0).asInt();
        }

        if (!json.isMember(CHOICES) || !json[CHOICES].isArray() || json[CHOICES].empty())
        {
            resp.error = "响应缺少 choices";
            LOG_WARN("{}", resp.error);
            return;
        }

        const Json::Value &message = json[CHOICES][0][MESSAGE];
        resp.content = message.get(CONTENT, "").asString();

        // 解析工具调用
        if (message.isMember("tool_calls") && message["tool_calls"].isArray())
        {
            for (const auto &call : message["tool_calls"])
            {
                ToolCall tc;
                tc.id = call.get("id", "").asString();
                if (call.isMember("function") && call["function"].isObject())
                {
                    tc.name = call["function"].get("name", "").asString();
                    tc.arguments = call["function"].get("arguments", "").asString();
                }
                if (!tc.name.empty())
                {
                    resp.tool_calls.push_back(std::move(tc));
                }
            }
        }

        resp.success = true;
        LOG_INFO("gpt响应解析成功 content:{} tool_calls:{}", resp.content, resp.tool_calls.size());
    }

    // 处理 SSE 事件流的单行解析
    void GPTProvider::processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback)
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
