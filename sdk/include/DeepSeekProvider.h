/**
 * @file DeepSeekProvider.h
 * @author yui
 */

// 使用deepseek作为模型提供者

#ifndef DEEPSEEKPROVIDER_H
#define DEEPSEEKPROVIDER_H

#include "LLMProvider.h"

namespace chat_sdk
{
    class DeepSeekProvider : public LLMProvider
    {
    public:
        // 模型初始化
        virtual bool initModel(const std::map<std::string, std::string> &model_config) override;
        // 检查模型是否有效
        virtual bool isAvailable() override
        {
            return isAvailable_;
        }
        // 获取模型名称(可通过配置的 model_name 覆盖,默认 deepseek-chat)
        virtual std::string getModelName() const override
        {
            return model_name_;
        }
        // 获取描述信息
        virtual std::string getModelDesc() const override
        {
            return "一款实用性强,中文友好的通用对话助手,适合日常问答与创作";
        }
        // 发送消息给模型 全量(支持工具)
        virtual LLMResponse sendMessage(const std::vector<Message> &messages,
                                        const std::map<std::string, std::string> &request_param,
                                        const std::vector<ToolDefinition> &tools = {}) override;
        // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
        virtual std::string sendMessageStream(const std::vector<Message> &messages,
                                              const std::map<std::string, std::string> &request_param, func_stream callback) override;

    private:
        std::string model_name_ = "deepseek-chat"; // 默认模型,可通过配置覆盖
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