/**
 * @file OpenAICompatProvider.h
 * @author yui
 */

// OpenAI Chat Completions 兼容 provider (三大协议族之一)
// 适配所有走 OpenAI 兼容端点的模型: gpt / deepseek / qwen(kimi/glm 等国内模型) / Ollama(/v1) / vLLM ...
// 协议族内部的差异全部由配置消化: base_url 指到哪、model_name 叫什么,无需新增代码
// api_key 可空(本地 Ollama / vLLM 等无鉴权端点)
// 支持 Function Calling: 注入 tools,解析 tool_calls

#ifndef OPENAI_COMPAT_PROVIDER_H
#define OPENAI_COMPAT_PROVIDER_H

#include "LLMProvider.h"

namespace chat_sdk
{
    class OpenAICompatProvider : public LLMProvider
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
        // 负责将 Message 列表和参数转为 JSON 字符串(含工具注入)
        std::string buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream,
                                     const std::vector<ToolDefinition> &tools);
        // 从响应 JSON 解析内容 / 工具调用 / token 用量,填充到 resp
        void parseResponse(const std::string &response_body, LLMResponse &resp);
        // 处理 SSE 事件流的单行解析
        void processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback);

    private:
        std::string model_name_ = "gpt-4o-mini"; // 默认模型,可通过配置覆盖
        std::string model_desc_ = "OpenAI 兼容通用模型,可通过配置指向任意 OpenAI 兼容端点";
    };
}

#endif