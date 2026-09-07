#include <mutex>
#include "../include/ProviderFactory.h"
#include "../include/OpenAICompatProvider.h"
#include "../include/ClaudeProvider.h"
#include "../include/GeminiProvider.h"
#include "../include/logger.h"

namespace chat_sdk
{
    ProviderFactory &ProviderFactory::instance()
    {
        static ProviderFactory factory;
        return factory;
    }

    bool ProviderFactory::registerProvider(const std::string &type, Creator creator)
    {
        if (type.empty() || !creator)
        {
            LOG_ERROR("provider 类型或创建函数非法:{}", type);
            return false;
        }
        auto it = creators_.find(type);
        if (it != creators_.end())
        {
            LOG_WARN("provider 类型 {} 重复注册,将被覆盖", type);
        }
        creators_[type] = std::move(creator);
        LOG_INFO("注册 provider 类型:{}", type);
        return true;
    }

    std::unique_ptr<LLMProvider> ProviderFactory::create(const std::string &type) const
    {
        auto it = creators_.find(type);
        if (it == creators_.end())
        {
            LOG_ERROR("未注册的 provider 类型:{}", type);
            return nullptr;
        }
        auto provider = it->second();
        if (!provider)
        {
            LOG_ERROR("provider 类型 {} 创建失败", type);
            return nullptr;
        }
        return provider;
    }

    std::vector<std::string> ProviderFactory::supportedTypes() const
    {
        std::vector<std::string> types;
        types.reserve(creators_.size());
        for (const auto &pair : creators_)
        {
            types.push_back(pair.first);
        }
        return types;
    }

    // 注册内置协议族。用 once_flag 保证幂等,避免 ChatSDK 被多次初始化时重复注册。
    // 三大协议族: openai(OpenAI 兼容,DeepSeek/Ollama/Qwen 等都走这一套) / claude / gemini
    // 其他模型一律通过这三者之一 + base_url/model_name 配置接入,无需新增 provider。
    void registerBuiltinProviders()
    {
        static std::once_flag flag;
        std::call_once(flag, []() {
            auto &factory = ProviderFactory::instance();
            factory.registerProvider("openai", []() { return std::make_unique<OpenAICompatProvider>(); });
            factory.registerProvider("gpt", []() { return std::make_unique<OpenAICompatProvider>(); }); // 兼容别名
            factory.registerProvider("claude", []() { return std::make_unique<ClaudeProvider>(); });
            factory.registerProvider("gemini", []() { return std::make_unique<GeminiProvider>(); });
        });
    }
}
