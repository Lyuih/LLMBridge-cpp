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
        // 获取模型名称
        virtual std::string getModelName() const override
        {
            return "gemini-2.5-flash-lite";
        }
        // 获取描述信息
        virtual std::string getModelDesc() const override
        {
            return "Google 的急速响应模型，转为大模型部署和快速交互的场景设计";
        }
        // 发送消息给模型
        virtual std::string sendMessage(const std::vector<Message> &messages,
                                        const std::map<std::string, std::string> &request_param) override;
        // 发送消息给模型 流式响应(每生成几个字符就触发回调函数)
        virtual std::string sendMessageStream(const std::vector<Message> &messages,
                                              const std::map<std::string, std::string> &request_param, func_stream callback) override;

    private:
        // 负责将 Message 列表和参数转为 JSON 字符串
        std::string buildRequestBody(const std::vector<Message> &messages, double temp, int max_tokens, bool stream);
        // 负责从返回的 JSON 中提取文本内容
        std::string extractContentFromJson(const std::string &response_body);
        // 处理 SSE 事件流的单行解析
        void processSseEvent(const std::string &event, std::string &full_content, bool &streamFinish, func_stream callback);
    };
}
#endif