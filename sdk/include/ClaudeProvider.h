/**
 * @file ClaudeProvider.h
 * @author yui
 */

// Anthropic Messages API provider (三大协议族之一)
// 适配 Claude 及提供 POST /v1/messages 兼容端点的模型(部分厂商的 Claude Code 兼容网关)
// 与 OpenAI 兼容族的协议差异:
//   - 鉴权: x-api-key + anthropic-version 头(非 Bearer)
//   - system 是独立顶层字段,不信 messages
//   - 工具: schema 用 input_schema;调用回填是 content block(tool_use / tool_result)
//   - 流式: message_start / content_block_delta / message_stop 事件族,与 OpenAI SSE 不同

#ifndef CLAUDEPROVIDER_H
#define CLAUDEPROVIDER_H

#include "LLMProvider.h"

namespace chat_sdk
{
    class ClaudeProvider : public LLMProvider
    {
    public:
        // 模型初始化
        virtual bool initModel(const std::map<std::string, std::string> &model_config) override;
        // 检查模型是否有效
        virtual bool isAvailable() override
        {
            return isAvailable_;
        }
        // 获取模型名称
        virtual std::string getModelName() const override
        {
            return model_name_;
        }
        // 获取描述信息
        virtual std::string getModelDesc() const override
        {
            return model_desc_;
        }
        // 发送消息给模型 全量(支持工具)
        virtual LLMResponse sendMessage(const std::vector<Message> &messages,
                                        const std::map<std::string, std::string> &request_param,
                                        const std::vector<ToolDefinition> &tools = {}) override;
        // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
        virtual std::string sendMessageStream(const std::vector<Message> &messages,
                                              const std::map<std::string, std::string> &request_param, func_stream callback) override;

    private:
        // 负责将 Message 列表和参数转为 Anthropic 格式 JSON 字符串(含工具注入与 tool_result 回填)
        std::string buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream,
                                     const std::vector<ToolDefinition> &tools);
        // 从响应 JSON 解析 content blocks / tool_use / token 用量,填充到 resp
        void parseResponse(const std::string &response_body, LLMResponse &resp);
        // 处理 SSE 事件(content_block_delta 文本 / message_stop 结束 / error)
        void processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback);

    private:
        std::string model_name_ = "claude-3-5-sonnet-latest"; // 默认模型,可通过配置覆盖
        std::string model_desc_ = "Anthropic Messages API 兼容模型,可接入 Claude 及提供 /v1/messages 兼容端点的模型";
    };
}

#endif