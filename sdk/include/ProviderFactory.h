/**
 * @file ProviderFactory.h
 * @author yui
 */

// provider 插件化工厂
// 通过注册表把 "provider 类型字符串" 映射到 "创建函数",ChatSDK 初始化时按配置的
// provider_type 创建对应 provider。新增 provider 只需 registerProvider 一行,不改任何调用方。

#ifndef PROVIDERFACTORY_H
#define PROVIDERFACTORY_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "LLMProvider.h"

namespace chat_sdk
{
    class ProviderFactory
    {
    public:
        using Creator = std::function<std::unique_ptr<LLMProvider>()>;

        // 单例
        static ProviderFactory &instance();

        // 注册一个 provider 类型。重复注册同名类型会覆盖并告警。
        bool registerProvider(const std::string &type, Creator creator);

        // 按类型创建 provider,未知类型返回 nullptr
        std::unique_ptr<LLMProvider> create(const std::string &type) const;

        // 当前已注册的类型列表
        std::vector<std::string> supportedTypes() const;

    private:
        ProviderFactory() = default;
        std::map<std::string, Creator> creators_;
    };

    // 注册内置协议族: "openai" / "gpt"(别名) / "claude" / "gemini"
    // 模型差异(deepseek/ollama/qwen...)统一由配置消化,不再按模型注册 provider
    // 在 ChatSDK 首次初始化前调用一次即可(幂等)
    void registerBuiltinProviders();
}

#endif