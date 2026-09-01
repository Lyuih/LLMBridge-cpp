/**
 * @file GeminiProvider.h
 * @author yui
 */

#ifndef GEMINIPROVIDER_H
#define GEMINIPROVIDER_H
#include "LLMProvider.h"

namespace chat_sdk
{
    class GeminiProvider : public LLMProvider
    {
    public:
        // 模型初始化
        virtual bool initModel(const std::map<std::string, std::string> &model_config) override;
        // 检查模型是否有效
        virtual bool isAvailable() override
        {
            return isAvailable_;
        }
        // 获取模型名称(可通过配置的 model_name 覆盖,默认 gemini-2.5-flash-lite)
        virtual std::string getModelName() const override
        {
            return model_name_;
        }
        // 获取描述信息
        virtual std::string getModelDesc() const override
        {
            return "Google 的急速响应模型，转为大模型部署和快速交互的场景设计";
        }
        // 发送消息给模型 全量(支持工具)
        virtual LLMResponse sendMessage(const std::vector<Message> &messages,
                                        const std::map<std::string, std::string> &request_param,
                                        const std::vector<ToolDefinition> &tools = {}) override;
        // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
        virtual std::string sendMessageStream(const std::vector<Message> &messages,
                                              const std::map<std::string, std::string> &request_param, func_stream callback) override;

    private:
        std::string model_name_ = "gemini-2.5-flash-lite"; // 默认模型,可通过配置覆盖
        // 负责将 Message 列表和参数转为 JSON 字符串(含工具注入)
        std::string buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream,
                                     const std::vector<ToolDefinition> &tools);
        // 从响应 JSON 解析内容 / 工具调用 / token 用量
        void parseResponse(const std::string &response_body, LLMResponse &resp);
        // 处理 SSE 事件流的单行解析
        void processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback);
    };
}
#endif