#include <algorithm>
#include <chrono>
#include "../include/ChatSDK.h"
#include "../include/ProviderFactory.h"
#include "../include/ConfigLoader.h"
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
        if (configs.empty())
        {
            LOG_ERROR("模型配置列表为空,初始化失败");
            return false;
        }
        // 注册所有支持模型提供者
        registerAllProvider(configs);
        // 初始化模型
        initProviders(configs);
        // 构建路由(加权 + 故障转移链 + 虚拟路由组)
        buildRouter(configs);
        initialized_ = true;
        return true;
    }
    // 初始化模型 配置驱动,支持任意模型
    bool ChatSDK::initFromConfigFile(const std::string &config_path)
    {
        std::string error;
        auto configs = ConfigLoader::loadFromFile(config_path, error);
        if (configs.empty())
        {
            LOG_ERROR("配置文件加载失败:{}", error);
            return false;
        }
        return initModels(configs);
    }
    // 初始化模型 配置驱动(字符串形式)
    bool ChatSDK::initFromConfigString(const std::string &json_str)
    {
        std::string error;
        auto configs = ConfigLoader::loadFromString(json_str, error);
        if (configs.empty())
        {
            LOG_ERROR("配置字符串加载失败:{}", error);
            return false;
        }
        return initModels(configs);
    }
    // 创建session
    std::string ChatSDK::createSession(const std::string model_name)
    {
        if (!initialized_)
        {
            LOG_WARN("SDK未初始化");
            return "";
        }

        // 增加对 model_name 的合法性校验
        if (configs_.find(model_name) == configs_.end())
        {
            LOG_ERROR("模型未初始化或不支持:{}", model_name);
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
        Message user_message("user", message);
        sessionManager_.addMessage(session_id, user_message);

        // 构建请求参数
        auto it_config = configs_.find(session->model_name);
        if (it_config == configs_.end())
        {
            LOG_ERROR("配置参数没找到:{}", session->model_name);
            return "";
        }

        std::map<std::string, std::string> request_params;
        request_params[TEMPERATURE] = std::to_string(it_config->second->temperature);
        request_params[MAX_TOKENS] = std::to_string(it_config->second->max_tokens);

        // 获取历史会话
        std::vector<Message> history = sessionManager_.getSessionHistory(session_id);

        // 上下文 / Token 管理: 按预算裁剪历史(context_window 未配置则不裁剪)
        int context_budget = 0;
        if (it_config->second->context_window > 0)
        {
            context_budget = std::max(1, it_config->second->context_window - it_config->second->max_tokens);
        }
        history = contextManager_.fitToBudget(history, context_budget);

        // 模型路由 / 故障转移: 计算尝试顺序,失败自动切换
        std::vector<std::string> attempt_order = router_.plan(session->model_name);
        if (attempt_order.empty())
        {
            LOG_ERROR("模型 {} 无可用后端", session->model_name);
            return "";
        }

        // Function Calling 工具执行循环: 模型请求工具 -> 执行 -> 结果回填 -> 再次调用模型
        const int max_tool_rounds = 5;
        std::string final_text;
        bool done = false;
        auto start = std::chrono::steady_clock::now();
        for (int round = 0; round <= max_tool_rounds && !done; ++round)
        {
            int round_input = contextManager_.countMessagesTokens(history);
            auto round_start = std::chrono::steady_clock::now();
            std::string used_target;
            LLMResponse resp;
            for (const auto &target : attempt_order)
            {
                resp = llmManager_.sendMessage(target, history, request_params, toolRegistry_.definitions());
                if (resp.success)
                {
                    used_target = target;
                    break;
                }
                router_.markUnavailable(target);
                LOG_WARN("模型 {} 调用失败:{}", target, resp.error);
            }
            double round_elapsed = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - round_start).count();

            if (!resp.success || used_target.empty())
            {
                MetricsCollector::instance().record(session->model_name, round_elapsed, false, round_input, 0);
                LOG_ERROR("模型 {} 全部候选调用失败", session->model_name);
                return "";
            }

            // 记录本轮调用指标(优先用 provider 返回的真实 token 用量)
            int output_tokens = resp.output_tokens > 0 ? resp.output_tokens
                                                       : ContextManager::estimateTokens(resp.content);
            MetricsCollector::instance().record(used_target, round_elapsed, true, round_input, output_tokens);
            router_.markAvailable(used_target);
            LOG_INFO("模型 {} 调用成功,耗时 {}ms, tool_calls:{}", used_target, round_elapsed, resp.tool_calls.size());

            // 模型请求调用工具: 执行并回填,进入下一轮
            if (resp.hasToolCalls())
            {
                if (round == max_tool_rounds)
                {
                    LOG_WARN("工具调用轮次超过上限,返回当前文本");
                    break;
                }
                Message assistant_msg("assistant", resp.content);
                assistant_msg.tool_calls = resp.tool_calls;
                history.push_back(std::move(assistant_msg));
                for (const auto &tc : resp.tool_calls)
                {
                    std::string result = toolRegistry_.execute(tc.name, tc.arguments);
                    Message tool_msg("tool", result);
                    tool_msg.tool_call_id = tc.id;
                    tool_msg.tool_name = tc.name;
                    history.push_back(std::move(tool_msg));
                    LOG_INFO("工具 {} 执行并回填完成", tc.name);
                }
                continue;
            }

            // 最终文本回复
            final_text = resp.content;
            done = true;
        }

        if (final_text.empty())
        {
            MetricsCollector::instance().record(session->model_name, 0, false, 0, 0);
            LOG_ERROR("模型 {} 未生成有效回复", session->model_name);
            return "";
        }

        // 添加助手响应并更新会话时间
        Message assistanMsg("assistant", final_text);
        sessionManager_.addMessage(session_id, assistanMsg);
        return final_text;
    }
    // 发送消息 流
    std::string ChatSDK::sendMessageStream(const std::string &session_id, const std::string &message,
                                           const LLMProvider::func_stream &call_back)
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
        Message user_message("user", message);
        sessionManager_.addMessage(session_id, user_message);

        // 构建请求参数
        auto it_config = configs_.find(session->model_name);
        if (it_config == configs_.end())
        {
            LOG_ERROR("配置参数没找到:{}", session->model_name);
            return "";
        }

        std::map<std::string, std::string> request_params;
        request_params[TEMPERATURE] = std::to_string(it_config->second->temperature);
        request_params[MAX_TOKENS] = std::to_string(it_config->second->max_tokens);

        // 获取历史会话
        std::vector<Message> history = sessionManager_.getSessionHistory(session_id);

        // 上下文 / Token 管理: 按预算裁剪历史
        int context_budget = 0;
        if (it_config->second->context_window > 0)
        {
            context_budget = std::max(1, it_config->second->context_window - it_config->second->max_tokens);
        }
        history = contextManager_.fitToBudget(history, context_budget);
        int input_tokens = contextManager_.countMessagesTokens(history);

        // 模型路由 / 故障转移: 流式失败(建连失败/非200)时切换候选
        std::vector<std::string> attempt_order = router_.plan(session->model_name);
        if (attempt_order.empty())
        {
            LOG_ERROR("模型 {} 无可用后端", session->model_name);
            return "";
        }

        std::string response;
        auto start = std::chrono::steady_clock::now();
        for (const auto &target : attempt_order)
        {
            response = llmManager_.sendMessageStream(target, history, request_params, call_back);
            if (!response.empty() && response != "null")
            {
                router_.markAvailable(target);
                auto elapsed = std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - start).count();
                MetricsCollector::instance().record(target, elapsed, true, input_tokens,
                                                    ContextManager::estimateTokens(response));
                LOG_INFO("模型 {} 流式调用成功,耗时 {}ms", target, elapsed);
                break;
            }
            router_.markUnavailable(target);
            LOG_WARN("模型 {} 流式调用失败,尝试下一个候选", target);
        }

        if (response.empty() || response == "null")
        {
            auto elapsed = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - start).count();
            MetricsCollector::instance().record(session->model_name, elapsed, false, input_tokens, 0);
            LOG_ERROR("模型 {} 全部候选流式调用失败", session->model_name);
            return "";
        }

        // 添加助手响应并更新会话时间
        Message assistanMsg("assistant", response);
        sessionManager_.addMessage(session_id, assistanMsg);
        return response;
    }

    // 注册所有模型 —— 配置驱动,通过 ProviderFactory 按 provider_type 创建,不再硬编码 model_name
    void ChatSDK::registerAllProvider(const std::vector<std::shared_ptr<Config>> &configs)
    {
        registerBuiltinProviders(); // 幂等
        auto &factory = ProviderFactory::instance();

        for (auto &config : configs)
        {
            auto provider = factory.create(config->provider_type);
            if (!provider)
            {
                LOG_ERROR("{} 的 provider 类型 {} 未注册,跳过", config->model_name, config->provider_type);
                continue;
            }
            llmManager_.registerProvider(config->model_name, std::move(provider));
            LOG_INFO("{} 注册成功(type:{})", config->model_name, config->provider_type);
        }
    }
    // 初始化所有模型提供者 —— 三大协议族统一走 ApiConfig,按 provider_type 驱动工厂创建
    void ChatSDK::initProviders(const std::vector<std::shared_ptr<Config>> &configs)
    {
        for (const auto &config : configs)
        {
            initAPIModelProviders(config->model_name, std::dynamic_pointer_cast<ApiConfig>(config));
        }
    }

    bool ChatSDK::initAPIModelProviders(const std::string &model_name, const std::shared_ptr<ApiConfig> &api_config)
    {
        if (model_name.empty())
        {
            LOG_ERROR("model_name为空");
            return false;
        }

        // api_key 可为空(本地 Ollama/vLLM 等无鉴权端点也走 OpenAI 兼容协议,不要求必须有 key)
        if (!api_config || api_config->endPoint_.empty())
        {
            LOG_ERROR("base_url为空");
            return false;
        }

        // 初始化模型
        std::map<std::string, std::string> model_params;
        model_params["api_key"] = api_config->api_key;
        model_params["base_url"] = api_config->endPoint_;
        model_params["model_name"] = model_name; // 让 provider 的 getModelName() 与外部模型名一致
        if (!api_config->model_desc_.empty())
        {
            model_params["model_desc"] = api_config->model_desc_;
        }
        if (!llmManager_.initModel(model_name, model_params))
        {
            return false;
        }
        // 模型配置
        configs_[model_name] = api_config;
        LOG_INFO("模型 {} 初始化成功", model_name);
        return true;
    }

    // 根据配置构建路由: 真实后端注册权重与故障转移链,虚拟路由组注册路由
    void ChatSDK::buildRouter(const std::vector<std::shared_ptr<Config>> &configs)
    {
        for (const auto &config : configs)
        {
            if (!config->provider_type.empty())
            {
                router_.addBackend(config->model_name, config->weight, config->fallback);
            }
            if (!config->route.empty())
            {
                router_.addRoute(config->model_name, config->route);
                // 路由组自身配置也要可查,便于会话读取温度等参数
                configs_[config->model_name] = config;
            }
        }
    }

    // 可观测性: 获取全部模型指标
    std::map<std::string, ModelMetrics> ChatSDK::getMetrics() const
    {
        return MetricsCollector::instance().getAll();
    }

    // 可观测性: 指标 JSON 字符串
    std::string ChatSDK::getMetricsJson() const
    {
        return MetricsCollector::instance().toJson();
    }

    // 路由组列表
    std::vector<std::string> ChatSDK::getRouteNames() const
    {
        return router_.routeNames();
    }

    // 注册可执行工具
    void ChatSDK::registerTool(std::shared_ptr<Tool> tool)
    {
        toolRegistry_.registerTool(std::move(tool));
    }

    // 已注册工具列表
    std::vector<std::string> ChatSDK::getToolNames() const
    {
        return toolRegistry_.names();
    }

}