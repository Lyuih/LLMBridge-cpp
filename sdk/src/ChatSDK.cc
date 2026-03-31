#include "../include/ChatSDK.h"
#include "../include/DeepSeekProvider.h"
#include "../include/GPTProvider.h"
#include "../include/GeminiProvider.h"
#include "../include/logger.h"
#include "../include/fields.h"

namespace chat_sdk
{
    /*
        bool initialized_;
        std::unordered_map<std::string, std::shared_ptr<Config>> configs_;
        LLMManager llmManager_;
        SessionManager sessionManager_;
    */

    // 初始化模型
    bool ChatSDK::initModels(const std::vector<std::shared_ptr<Config>> &configs)
    {
        // 注册所有支持模型提供者
        registerAllProvider(configs);
        // 初始化模型
        initProviders(configs);
        initialized_ = true;
        return true;
    }
    // 创建session
    std::string ChatSDK::createSession(const std::string model_name)
    {
        if (!initialized_)
        {
            LOG_WARN("SDK未初始化");
            return "";
        }
        std::string session_id = sessionManager_.createSession(model_name);
        LOG_INFO("创建会话成功id:{},model_name:{}", session_id, model_name);
        return session_id;
    }
    // 获取会话
    std::shared_ptr<Session> ChatSDK::getSession(const std::string &session_id)
    {
        return sessionManager_.getSession(session_id);
    }
    // 删除会话
    bool ChatSDK::deleteSession(const std::string &session_id)
    {
        return sessionManager_.deleteSession(session_id);
    }
    // 获取所有会话列表
    std::vector<std::string> ChatSDK::getSessionList()
    {
        return sessionManager_.getSessionList();
    }
    // 获取可用模型列表
    std::vector<ModelInfo> ChatSDK::getAvailableModels()
    {
        return llmManager_.getAvailableModel();
    }
    // 发送消息 全量
    std::string ChatSDK::sendMessage(const std::string &session_id, const std::string &message)
    {
        using namespace json_fields;
        if (!initialized_)
        {
            LOG_WARN("SDK未初始化");
            return "";
        }
        // 获取当前会话的session对象
        auto session = sessionManager_.getSession(session_id);
        if (!session)
        {
            LOG_ERROR("session_id {} 没找到", session_id);
            return "";
        }

        // 构建消息并添加到会话
        Message user_message("user",message);
        sessionManager_.addMessage(session_id, user_message);

        // 构建请求参数
        auto it_config = configs_.find(session->model_name);
        if (it_config == configs_.end())
        {
            LOG_ERROR("配置参数没找到:{}", session->model_name);
            return "";
        }

        std::map<std::string, std::string> request_params;
        request_params[TEMPERATURE] = std::to_string(it_config->second->temperture);
        request_params[MAX_TOKENS] = std::to_string(it_config->second->max_tokens);

        // 给模型发生请求
        // 获取历史会话
        std::vector<Message> history = sessionManager_.getSessionHistory(session_id);
        std::string response = llmManager_.sendMessage(session->model_name, history, request_params);
        // 添加助手响应并更新会话时间
        Message assistanMsg("assistant", response);
        sessionManager_.addMessage(session_id, assistanMsg);
        // sessionManager_.updateSessionTimestamp(session_id);
        return response;
    }
    // 发送消息 流
    std::string ChatSDK::sendMessageStream(const std::string &session_id, const std::string &message,
                                           LLMProvider::func_stream &call_back)
    {
        using namespace json_fields;
        if (!initialized_)
        {
            LOG_WARN("SDK未初始化");
            return "";
        }
        // 获取当前会话的session对象
        auto session = sessionManager_.getSession(session_id);
        if (!session)
        {
            LOG_ERROR("session_id {} 没找到", session_id);
            return "";
        }

        // 构建消息并添加到会话
        Message user_message("user",message);
        sessionManager_.addMessage(session_id, user_message);

        // 构建请求参数
        auto it_config = configs_.find(session->model_name);
        if (it_config == configs_.end())
        {
            LOG_ERROR("配置参数没找到:{}", session->model_name);
            return "";
        }

        std::map<std::string, std::string> request_params;
        request_params[TEMPERATURE] = std::to_string(it_config->second->temperture);
        request_params[MAX_TOKENS] = std::to_string(it_config->second->max_tokens);

        // 给模型发生请求
        // 获取历史会话
        std::vector<Message> history = sessionManager_.getSessionHistory(session_id);
        std::string response = llmManager_.sendMessageStream(session->model_name,
                                                             history, request_params, call_back);
        // 添加助手响应并更新会话时间
        Message assistanMsg("assistant", response);
        sessionManager_.addMessage(session_id, assistanMsg);
        // sessionManager_.updateSessionTimestamp(session_id);
        return response;
    }

    // 注册所有模型
    void ChatSDK::registerAllProvider(const std::vector<std::shared_ptr<Config>> &configs)
    {

        for (auto &config : configs)
        {
            
            if (auto api_config = std::dynamic_pointer_cast<ApiConfig>(config))
            {
                auto deepseek_provider = std::make_unique<DeepSeekProvider>();
                llmManager_.registerProvider(api_config->model_name, std::move(deepseek_provider));
                LOG_INFO("{}注册成功", api_config->model_name);
            }
            else if(auto ollama_config = std::dynamic_pointer_cast<OllamaConfig>(config))
            {
                // TODO_ Ollama接入
            }
            else
            {

            }
        }

    }
    // 初始化所有模型提供者
    void ChatSDK::initProviders(const std::vector<std::shared_ptr<Config>> &configs)
    {
        for (const auto &config : configs)
        {
            if (auto apiConfig = std::dynamic_pointer_cast<ApiConfig>(config))
            {
                if (apiConfig->model_name == "deepseek-chat" ||
                    apiConfig->model_name == "gemini-2.0-flash" ||
                    apiConfig->model_name == "chat-4o-mini")
                {
                    initAPIModelProviders(apiConfig->model_name, apiConfig);
                }
                else
                {
                    LOG_ERROR("{}模型不支持", apiConfig->model_name);
                }
            }
            else if (auto ollamaConfig = std::dynamic_pointer_cast<OllamaConfig>(config))
            {
                ; // TODO_ Ollama接入
            }
            else
            {
                LOG_ERROR("{}配置不支持", apiConfig->model_name);
            }
        }
    }
    bool ChatSDK::initAPIModelProviders(const std::string &model_name, const std::shared_ptr<ApiConfig> &api_config)
    {
        if (model_name.empty())
        {
            LOG_ERROR("model_name为空");
            return false;
        }

        if (!api_config || api_config->api_key.empty())
        {
            LOG_ERROR("api_key为空");
            return false;
        }

        // 初始化模型
        std::map<std::string, std::string> model_params;
        model_params["api_key"] = api_config->api_key;
        model_params["base_url"] = api_config->endPoint_;
        if (!llmManager_.initModel(model_name, model_params))
        {
            return false;
        }
        // 模型配置
        configs_[model_name] = api_config;
        LOG_INFO("模型 {} 初始化成功", model_name);
        return true;
    }
}