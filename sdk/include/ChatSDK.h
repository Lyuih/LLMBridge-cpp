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
#include "Router.h"
#include "ContextManager.h"
#include "MetricsCollector.h"
#include "Tool.h"

namespace chat_sdk
{
    class ChatSDK
    {
    public:
        // 初始化模型(程序化传入配置)
        bool initModels(const std::vector<std::shared_ptr<Config>> &configs);
        // 初始化模型(从 JSON 配置文件加载,配置驱动,支持任意模型)
        bool initFromConfigFile(const std::string &config_path);
        // 初始化模型(从 JSON 字符串加载)
        bool initFromConfigString(const std::string &json_str);
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
                                      const LLMProvider::func_stream &call_back);
        // 可观测性: 获取全部模型指标
        std::map<std::string, ModelMetrics> getMetrics() const;
        // 可观测性: 指标 JSON 字符串
        std::string getMetricsJson() const;
        // 路由组列表
        std::vector<std::string> getRouteNames() const;
        // 注册可执行工具(Function Calling)
        void registerTool(std::shared_ptr<Tool> tool);
        // 已注册工具列表
        std::vector<std::string> getToolNames() const;

    private:
        // 根据配置构建路由(加权 + 故障转移链 + 虚拟路由组)
        void buildRouter(const std::vector<std::shared_ptr<Config>> &configs);
        // 注册所有模型
        void registerAllProvider(const std::vector<std::shared_ptr<Config>> &configs);
        // 初始化所有模型提供者
        void initProviders(const std::vector<std::shared_ptr<Config>> &configs);
        // 初始化模型提供者 通过ollama
        bool initOllamaModelProviders(const std::string &modelName, const std::shared_ptr<OllamaConfig> &ollamaConfig);
        // 初始化模型提供者 - API模型提供者
        bool initAPIModelProviders(const std::string &model_name, const std::shared_ptr<ApiConfig> &api_config);

    private:
        bool initialized_;
        // 模型名称 -> 模型配置信息
        std::unordered_map<std::string, std::shared_ptr<Config>> configs_;
        LLMManager llmManager_;
        // 模型路由 / 故障转移
        Router router_;
        // 上下文 / Token 管理
        ContextManager contextManager_;
        // 可执行工具注册表(Function Calling)
        ToolRegistry toolRegistry_;

    public:
        SessionManager sessionManager_;
    };
}

#endif