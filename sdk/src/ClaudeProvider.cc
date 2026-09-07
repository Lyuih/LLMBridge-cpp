#include <sstream>
#include <json/json.h>
#include "../include/httplib.h"
#include "../include/ClaudeProvider.h"
#include "../include/logger.h"

namespace chat_sdk
{
    namespace
    {
        // 把 ToolDefinition 转成 Anthropic 格式的工具对象
        // 差异: schema 字段名是 input_schema,不是 parameters
        Json::Value toolToToolDef(const ToolDefinition &def)
        {
            Json::Value t;
            t["name"] = def.name;
            if (!def.description.empty())
            {
                t["description"] = def.description;
            }
            Json::Value schema;
            schema["type"] = "object";
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
                schema["properties"] = props;
            }
            if (!required.empty())
            {
                schema["required"] = required;
            }
            t["input_schema"] = schema;
            return t;
        }

        // 去掉首尾空白
        std::string trim(const std::string &s)
        {
            size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos)
            {
                return "";
            }
            size_t e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        }
    }

    // 模型初始化
    bool ClaudeProvider::initModel(const std::map<std::string, std::string> &model_config)
    {
        // 1. 获取 base url(必填)
        auto it = model_config.find("base_url");
        if (it == model_config.end() || it->second.empty())
        {
            LOG_ERROR("claude获取base_url失败!");
            return false;
        }
        endPoint_ = it->second;
        // 2. 可选: api_key(兼容网关注入)
        it = model_config.find("api_key");
        if (it != model_config.end())
        {
            api_key_ = it->second;
        }
        // 3. 可选: 配置自定义模型名(默认 claude-3-5-sonnet-latest)
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
        LOG_INFO("claude模型初始化成功 {} model:{}", endPoint_, model_name_);
        return true;
    }

    // 发送消息给模型 全量返回(支持工具)
    LLMResponse ClaudeProvider::sendMessage(const std::vector<Message> &messages,
                                            const std::map<std::string, std::string> &request_param,
                                            const std::vector<ToolDefinition> &tools)
    {
        LLMResponse resp;
        if (!isAvailable())
        {
            resp.error = "claude模型失效";
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
            {"anthropic-version", "2023-06-01"},
            {"Content-Type", "application/json"}};
        // Claude 鉴权用 x-api-key,而非 Bearer
        if (!api_key_.empty())
        {
            headers.emplace("x-api-key", api_key_);
        }
        auto response = client.Post("/v1/messages", headers, json_msg, "application/json");
        if (!response)
        {
            resp.error = "连接 Anthropic 兼容 API 失败,请检查网络和 ssl";
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        if (response->status != 200)
        {
            resp.error = "Anthropic 兼容 API 返回非200状态 " + std::to_string(response->status) + " - " + response->body;
            LOG_ERROR("{}", resp.error);
            return resp;
        }
        parseResponse(response->body, resp);
        return resp;
    }

    // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
    std::string ClaudeProvider::sendMessageStream(const std::vector<Message> &messages,
                                                  const std::map<std::string, std::string> &request_param, func_stream callback)
    {
        LOG_DEBUG("claude流式响应");
        if (!isAvailable())
        {
            LOG_ERROR("claude模型失效");
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
            {"anthropic-version", "2023-06-01"},
            {"Accept", "text/event-stream"},
            {"Content-Type", "application/json"}};
        if (!api_key_.empty())
        {
            headers.emplace("x-api-key", api_key_);
        }

        std::string buffer;
        bool gotError = false;
        std::string errorMsg;
        int statusCode = 0;
        bool streamFinish = false;
        std::string full_content;

        httplib::Request req;
        req.method = "POST";
        req.path = "/v1/messages";
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

    // 负责将 Message 列表和参数转为 Anthropic 格式 JSON 字符串
    // 关键差异处理:
    //   - system 消息抽到顶层 system 字段
    //   - role==tool 的消息合并成一条 user 消息的 tool_result content blocks
    //   - assistant 的 tool_calls 转成 tool_use blocks
    std::string ClaudeProvider::buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream,
                                                 const std::vector<ToolDefinition> &tools)
    {
        Json::Value msg_array(Json::arrayValue);
        std::string system_content;
        Json::Value tool_results(Json::arrayValue); // 待合并为 user 消息的 tool_result blocks

        // 把累积的 tool_result blocks 作为一条 user 消息刷出
        auto flush_tool_results = [&]()
        {
            if (tool_results.empty())
            {
                return;
            }
            Json::Value user_msg;
            user_msg["role"] = "user";
            user_msg["content"] = tool_results;
            msg_array.append(user_msg);
            tool_results.resize(0);
        };

        for (const auto &message : messages)
        {
            if (message.role == "system")
            {
                system_content += (system_content.empty() ? "" : "\n") + message.content;
                continue;
            }

            if (message.role == "tool")
            {
                // 工具结果: 收集为 tool_result block,与前后的 tool 消息合并成一条 user 消息
                Json::Value block;
                block["type"] = "tool_result";
                block["tool_use_id"] = message.tool_call_id;
                block["content"] = message.content;
                tool_results.append(block);
                continue;
            }

            // 遇到非 tool 消息,先刷出之前累积的工具结果(保证 tool_use 后紧跟 tool_result)
            flush_tool_results();

            if (message.role == "user")
            {
                Json::Value user_msg;
                user_msg["role"] = "user";
                user_msg["content"] = message.content;
                msg_array.append(user_msg);
            }
            else if (message.role == "assistant")
            {
                if (message.tool_calls.empty())
                {
                    Json::Value asst;
                    asst["role"] = "assistant";
                    asst["content"] = message.content;
                    msg_array.append(asst);
                }
                else
                {
                    Json::Value blocks(Json::arrayValue);
                    if (!message.content.empty())
                    {
                        Json::Value text_block;
                        text_block["type"] = "text";
                        text_block["text"] = message.content;
                        blocks.append(text_block);
                    }
                    for (const auto &tc : message.tool_calls)
                    {
                        Json::Value tool_use;
                        tool_use["type"] = "tool_use";
                        tool_use["id"] = tc.id;
                        tool_use["name"] = tc.name;
                        // 语义: 本地 ToolCall.arguments 是 JSON *字符串*,Anthropic input 要是对象
                        Json::Value input(Json::objectValue);
                        Json::CharReaderBuilder rb;
                        std::string errs;
                        std::istringstream iss(tc.arguments);
                        if (!tc.arguments.empty() && Json::parseFromStream(rb, iss, &input, &errs))
                        {
                            tool_use["input"] = input;
                        }
                        else
                        {
                            tool_use["input"] = Json::Value(Json::objectValue);
                        }
                        blocks.append(tool_use);
                    }
                    Json::Value asst;
                    asst["role"] = "assistant";
                    asst["content"] = blocks;
                    msg_array.append(asst);
                }
            }
            // 其他未知 role 忽略
        }
        flush_tool_results();

        Json::Value request_body;
        request_body["model"] = model_name_;
        request_body["max_tokens"] = max_tokens; // Anthropic 必填字段
        if (!system_content.empty())
        {
            request_body["system"] = system_content;
        }
        request_body["messages"] = msg_array;
        request_body["temperature"] = temp;
        if (stream)
        {
            request_body["stream"] = true;
        }
        if (!tools.empty())
        {
            Json::Value tools_json(Json::arrayValue);
            for (const auto &def : tools)
            {
                tools_json.append(toolToToolDef(def));
            }
            request_body["tools"] = tools_json;
        }
        Json::StreamWriterBuilder writer;
        std::string json_string = Json::writeString(writer, request_body);
        LOG_DEBUG("claude请求数据序列化成功:{}", json_string);
        return json_string;
    }

    // 从响应 JSON 解析 content blocks / tool_use / token 用量
    void ClaudeProvider::parseResponse(const std::string &response_body, LLMResponse &resp)
    {
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

        // token 用量(字段名与 OpenAI 不同: input_tokens / output_tokens)
        if (json.isMember("usage") && json["usage"].isObject())
        {
            resp.input_tokens = json["usage"].get("input_tokens", 0).asInt();
            resp.output_tokens = json["usage"].get("output_tokens", 0).asInt();
        }

        if (!json.isMember("content") || !json["content"].isArray())
        {
            resp.error = "响应缺少 content blocks";
            LOG_WARN("{}", resp.error);
            return;
        }

        const Json::Value &content = json["content"];
        for (const auto &block : content)
        {
            const std::string type = block.get("type", "").asString();
            if (type == "text")
            {
                resp.content += block.get("text", "").asString();
            }
            else if (type == "tool_use")
            {
                ToolCall tc;
                tc.id = block.get("id", "").asString();
                tc.name = block.get("name", "").asString();
                // 语义: input 是对象,序列化回 JSON *字符串*,与 OpenAI 族的 ToolCall.arguments 不变式一致
                Json::StreamWriterBuilder wb;
                tc.arguments = Json::writeString(wb, block["input"]);
                if (!tc.name.empty())
                {
                    resp.tool_calls.push_back(std::move(tc));
                }
            }
        }

        resp.success = true;
        LOG_INFO("claude响应解析成功 content:{} tool_calls:{}", resp.content, resp.tool_calls.size());
    }

    // 处理 SSE 事件
    // Anthropic 事件帧形如:
    //   event: content_block_delta
    //   data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"..."}}
    // 与 OpenAI 的 {choices:[{delta:{content}}]} 结构完全不同
    void ClaudeProvider::processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback)
    {
        if (event.empty())
        {
            return;
        }
        // 从多行事件中取出 data: 后的内容
        std::string data_line;
        std::istringstream line_stream(event);
        std::string line;
        while (std::getline(line_stream, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.rfind("data:", 0) == 0)
            {
                data_line = line.substr(5);
                break;
            }
        }
        std::string json_str = trim(data_line);
        if (json_str.empty())
        {
            return;
        }
        if (json_str == "[DONE]")
        {
            callback("", true);
            streamFinish = true;
            return;
        }

        Json::Value root;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(json_str);
        if (!Json::parseFromStream(readBuilder, json_stream, &root, &errs))
        {
            LOG_ERROR("claude SSE 反序列化失败:{}", errs);
            return;
        }

        const std::string type = root.get("type", "").asString();
        if (type == "content_block_delta")
        {
            const Json::Value &delta = root["delta"];
            if (delta.isObject() && delta.get("type", "").asString() == "text_delta")
            {
                std::string text = delta.get("text", "").asString();
                full_content += text;
                callback(text, false);
            }
        }
        else if (type == "message_stop")
        {
            callback("", true);
            streamFinish = true;
        }
        else if (type == "error")
        {
            LOG_ERROR("claude 流式错误:{}", json_str);
            callback("", true);
            streamFinish = true;
        }
    }
}