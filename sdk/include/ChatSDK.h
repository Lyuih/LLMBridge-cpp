/**
 * @file ChatSDK.h
 * @author yui
 */
#ifndef CHATSDK_H
#define CHATSDK_H
#include <memory>
#include "LLMManager.h"
#include "common.h"
#include "SessionManager.h"

namespace chat_sdk
{
    class ChatSDK
    {
    public:
        // 初始化模型
        bool initModels(const std::vector<std::shared_ptr<Config>> &configs);
        // 创建session
        std::string createSession(const std::string model_name);
        // 获取会话
        std::shared_ptr<Session> getSession(const std::string &session_id);
        // 删除会话
        bool deleteSession(const std::string &session_id);
        // 获取所有会话列表
        std::vector<std::string> getSessionList();
        // 获取可用模型列表
        std::vector<ModelInfo> getAvailableModels();
        // 发送消息 全量
        std::string sendMessage(const std::string &session_id, const std::string &message);
        // 发送消息 流
        std::string sendMessageStream(const std::string &session_id, const std::string &message,
                                      LLMProvider::func_stream &call_back);

    private:
        // 注册所有模型
        void registerAllProvider(const std::vector<std::shared_ptr<Config>> &configs);
        // 初始化所有模型提供者
        void initProviders(const std::vector<std::shared_ptr<Config>> &configs);
        // 初始化模型提供者 通过ollama
        // bool ollamaProviderInit(const std::stinrg &modelName, const std::shared_ptr<OllamaConfig> &ollamaConfig);
        // 初始化模型提供者 - API模型提供者
        bool initAPIModelProviders(const std::string &model_name, const std::shared_ptr<ApiConfig> &api_config);

    private:
        bool initialized_;
        // 模型名称 -> 模型配置信息
        std::unordered_map<std::string, std::shared_ptr<Config>> configs_;
        LLMManager llmManager_;

    public:
        SessionManager sessionManager_;
    };
}

#endif