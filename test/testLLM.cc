#include <gtest/gtest.h>
#include <algorithm>
#include "../sdk/include/logger.h"
#include "../sdk/include/OpenAICompatProvider.h"
#include "../sdk/include/ClaudeProvider.h"
#include "../sdk/include/GeminiProvider.h"
#include "../sdk/include/LLMManager.h"

#include "../sdk/include/ChatSDK.h"
#include "../sdk/include/ConfigLoader.h"
#include "../sdk/include/ProviderFactory.h"
#include "../sdk/include/Router.h"
#include "../sdk/include/ContextManager.h"
#include "../sdk/include/MetricsCollector.h"
#include "../sdk/include/Tool.h"
#include "../sdk/include/common.h"

// ==================== 纯逻辑测试(无需网络) ====================

TEST(ConfigLoaderTest, loadFromString)
{
    const std::string json = R"(
    {
      "models": [
        {
          "name": "deepseek-chat",
          "provider": "openai",
          "api_key": "sk-test",
          "base_url": "https://api.deepseek.com",
          "temperature": 0.5,
          "max_tokens": 1024,
          "weight": 3,
          "fallback": ["gpt-4o-mini", "gemini-2.5-flash-lite"]
        },
        {
          "name": "qwen2:1.5b",
          "provider": "openai",
          "base_url": "http://127.0.0.1:11434/v1"
        }
      ]
    })";
    std::string error;
    auto configs = chat_sdk::ConfigLoader::loadFromString(json, error);
    ASSERT_EQ(configs.size(), 2u);
    ASSERT_TRUE(error.empty());

    // API 模型
    const auto &c1 = configs[0];
    EXPECT_EQ(c1->model_name, "deepseek-chat");
    EXPECT_EQ(c1->provider_type, "openai");
    EXPECT_EQ(c1->temperature, 0.5);
    EXPECT_EQ(c1->max_tokens, 1024);
    EXPECT_EQ(c1->weight, 3);
    ASSERT_EQ(c1->fallback.size(), 2u);
    EXPECT_EQ(c1->fallback[0], "gpt-4o-mini");
    EXPECT_EQ(c1->fallback[1], "gemini-2.5-flash-lite");
    auto api = std::dynamic_pointer_cast<chat_sdk::ApiConfig>(c1);
    ASSERT_NE(api, nullptr);
    EXPECT_EQ(api->api_key, "sk-test");
    EXPECT_EQ(api->endPoint_, "https://api.deepseek.com");

    // 本地模型三大协议族统一走 ApiConfig,不带 api_key
    const auto &c2 = configs[1];
    EXPECT_EQ(c2->model_name, "qwen2:1.5b");
    EXPECT_EQ(c2->provider_type, "openai");
    EXPECT_EQ(c2->weight, 1); // 默认权重
    auto local = std::dynamic_pointer_cast<chat_sdk::ApiConfig>(c2);
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->endPoint_, "http://127.0.0.1:11434/v1");
}

TEST(ConfigLoaderTest, loadInvalidMissingName)
{
    const std::string json = R"({ "models": [ { "provider": "openai" } ] })";
    std::string error;
    auto configs = chat_sdk::ConfigLoader::loadFromString(json, error);
    EXPECT_TRUE(configs.empty());
    EXPECT_FALSE(error.empty());
}

TEST(ProviderFactoryTest, createKnownTypes)
{
    chat_sdk::registerBuiltinProviders();
    auto &factory = chat_sdk::ProviderFactory::instance();

    // 三大协议族: openai(OpenAI 兼容) / claude / gemini
    {
        auto p = factory.create("openai");
        ASSERT_NE(p, nullptr);
        EXPECT_NE(dynamic_cast<chat_sdk::OpenAICompatProvider *>(p.get()), nullptr);
    }
    {
        auto p = factory.create("gpt"); // 兼容别名,仍是 OpenAI 兼容协议
        ASSERT_NE(p, nullptr);
        EXPECT_NE(dynamic_cast<chat_sdk::OpenAICompatProvider *>(p.get()), nullptr);
    }
    {
        auto p = factory.create("claude");
        ASSERT_NE(p, nullptr);
        EXPECT_NE(dynamic_cast<chat_sdk::ClaudeProvider *>(p.get()), nullptr);
    }
    {
        auto p = factory.create("gemini");
        ASSERT_NE(p, nullptr);
        EXPECT_NE(dynamic_cast<chat_sdk::GeminiProvider *>(p.get()), nullptr);
    }
}

TEST(ProviderFactoryTest, createUnknownType)
{
    chat_sdk::registerBuiltinProviders();
    auto &factory = chat_sdk::ProviderFactory::instance();
    EXPECT_EQ(factory.create("不存在的类型"), nullptr);
}

TEST(OpenAICompatProviderTest, initModelLocalWithoutApiKey)
{
    // 本地 Ollama/vLLM 端点走 OpenAI 兼容协议,无需 api_key
    chat_sdk::OpenAICompatProvider provider;
    std::map<std::string, std::string> param;
    param["base_url"] = "http://127.0.0.1:11434/v1";
    param["model_name"] = "qwen2:1.5b";
    EXPECT_TRUE(provider.initModel(param));
    EXPECT_TRUE(provider.isAvailable());
    EXPECT_EQ(provider.getModelName(), "qwen2:1.5b");
}

TEST(ClaudeProviderTest, initModelWithoutNetwork)
{
    chat_sdk::ClaudeProvider provider;
    std::map<std::string, std::string> param;
    param["base_url"] = "https://api.anthropic.com";
    param["model_name"] = "claude-3-5-sonnet-20241022";
    EXPECT_TRUE(provider.initModel(param));
    EXPECT_TRUE(provider.isAvailable());
    EXPECT_EQ(provider.getModelName(), "claude-3-5-sonnet-20241022");
}

TEST(ChatSDKTest, configDrivenInit)
{
    auto sdk = std::make_shared<chat_sdk::ChatSDK>();
    ASSERT_TRUE(sdk != nullptr);

    // 配置驱动的 OpenAI 兼容本地模型,不需要网络即可验证初始化链路
    const std::string json = R"(
    {
      "models": [
        {
          "name": "qwen2:1.5b",
          "provider": "openai",
          "base_url": "http://127.0.0.1:11434/v1",
          "api_key": "local",
          "desc": "本地测试模型"
        }
      ]
    })";

    ASSERT_TRUE(sdk->initFromConfigString(json));

    // 配置驱动的模型应出现在可用列表里
    auto models = sdk->getAvailableModels();
    ASSERT_EQ(models.size(), 1u);
    EXPECT_EQ(models[0].name_, "qwen2:1.5b");

    // 会话可以正常创建
    auto session_id = sdk->createSession("qwen2:1.5b");
    ASSERT_FALSE(session_id.empty());
}

TEST(ChatSDKTest, configDrivenInitUnknownProvider)
{
    auto sdk = std::make_shared<chat_sdk::ChatSDK>();
    ASSERT_TRUE(sdk != nullptr);

    // provider 类型未注册时应跳过该模型而不是崩溃
    const std::string json = R"(
    {
      "models": [
        {
          "name": "bad-model",
          "provider": "不存在的类型",
          "base_url": "http://127.0.0.1:11434/v1"
        },
        {
          "name": "qwen2:1.5b",
          "provider": "openai",
          "base_url": "http://127.0.0.1:11434/v1"
        }
      ]
    })";
    ASSERT_TRUE(sdk->initFromConfigString(json));
    auto models = sdk->getAvailableModels();
    ASSERT_EQ(models.size(), 1u);
    EXPECT_EQ(models[0].name_, "qwen2:1.5b");
}

TEST(RouterTest, planSingleBackend)
{
    chat_sdk::Router router;
    router.addBackend("deepseek-chat", 3, {"gpt-4o-mini"});
    auto order = router.plan("deepseek-chat");
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "deepseek-chat"); // 主后端
    EXPECT_EQ(order[1], "gpt-4o-mini");  // fallback 链
}

TEST(RouterTest, planRouteGroupIncludesAllBackends)
{
    chat_sdk::Router router;
    router.addBackend("deepseek-chat", 3);
    router.addBackend("gpt-4o-mini", 1);
    router.addRoute("smart", {"deepseek-chat", "gpt-4o-mini"});
    auto order = router.plan("smart");
    ASSERT_EQ(order.size(), 2u);
    // 两个后端都在尝试顺序里
    EXPECT_NE(std::find(order.begin(), order.end(), "deepseek-chat"), order.end());
    EXPECT_NE(std::find(order.begin(), order.end(), "gpt-4o-mini"), order.end());
}

TEST(RouterTest, circuitBreakerUnavailable)
{
    chat_sdk::Router router;
    router.addBackend("a", 1);
    EXPECT_TRUE(router.isHealthy("a"));
    router.markUnavailable("a");
    EXPECT_FALSE(router.isHealthy("a"));
    router.markAvailable("a");
    EXPECT_TRUE(router.isHealthy("a"));
}

TEST(RouterTest, planUnknownModelReturnsEmpty)
{
    chat_sdk::Router router;
    auto order = router.plan("不存在的模型");
    EXPECT_TRUE(order.empty());
}

namespace
{
    // 重复一个 UTF-8 字符 n 次,构造真实的中文长文本
    std::string repeatUtf8Char(const std::string &ch, int n)
    {
        std::string s;
        s.reserve(ch.size() * n);
        for (int i = 0; i < n; ++i)
        {
            s += ch;
        }
        return s;
    }
}

TEST(ContextManagerTest, estimateTokens)
{
    using chat_sdk::ContextManager;
    // 英文约 4 字符/token
    int en = ContextManager::estimateTokens("hello world hello world hello world");
    EXPECT_GT(en, 0);
    // 40 个中文字 ≈ 28 token,80 个英文字 ≈ 20 token,中文更占 token
    std::string zh = repeatUtf8Char("中", 40);
    std::string latin = std::string(80, 'a');
    int zh_tokens = ContextManager::estimateTokens(zh);
    int latin_tokens = ContextManager::estimateTokens(latin);
    EXPECT_GT(zh_tokens, 0);
    EXPECT_GT(zh_tokens, latin_tokens);
}

TEST(ContextManagerTest, fitToBudgetKeepsShortHistory)
{
    chat_sdk::ContextManager cm;
    std::vector<chat_sdk::Message> messages;
    messages.push_back(chat_sdk::Message("user", "你好"));
    messages.push_back(chat_sdk::Message("assistant", "你好!有什么可以帮你?"));
    auto result = cm.fitToBudget(messages, 2048);
    EXPECT_EQ(result.size(), 2u); // 预算充足不裁剪
}

TEST(ContextManagerTest, fitToBudgetTrimsOverflow)
{
    chat_sdk::ContextManager cm;
    std::vector<chat_sdk::Message> messages;
    std::string long_msg = repeatUtf8Char("长", 200); // 200 个汉字,约 140 token
    for (int i = 0; i < 50; ++i)
    {
        messages.push_back(chat_sdk::Message("user", long_msg));
        messages.push_back(chat_sdk::Message("assistant", long_msg));
    }
    auto result = cm.fitToBudget(messages, 500);
    EXPECT_LT(result.size(), messages.size()); // 被裁剪
    // 保留最近的 user 消息,且有摘要前缀
    bool has_summary = !result.empty() && result[0].role == "system";
    EXPECT_TRUE(has_summary);
    EXPECT_EQ(result.back().role, "assistant");
}

TEST(MetricsCollectorTest, recordAndQuery)
{
    chat_sdk::MetricsCollector::instance().reset();
    chat_sdk::MetricsCollector::instance().record("deepseek-chat", 100.0, true, 10, 20);
    chat_sdk::MetricsCollector::instance().record("deepseek-chat", 200.0, false, 10, 0);

    auto m = chat_sdk::MetricsCollector::instance().getModelMetrics("deepseek-chat");
    EXPECT_EQ(m.request_count, 2);
    EXPECT_EQ(m.success_count, 1);
    EXPECT_EQ(m.error_count, 1);
    EXPECT_NEAR(m.errorRate(), 0.5, 0.001);
    EXPECT_NEAR(m.avgLatencyMs(), 150.0, 0.001);
    EXPECT_EQ(m.total_input_tokens, 20);
    EXPECT_EQ(m.total_output_tokens, 20);

    std::string json = chat_sdk::MetricsCollector::instance().toJson();
    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("deepseek-chat"), std::string::npos);
}

// ==================== Function Calling 纯逻辑测试 ====================

namespace
{
    // 脚本化假 provider: 第一次请求工具,第二次返回最终答案
    class FakeToolProvider : public chat_sdk::LLMProvider
    {
    public:
        int call_count = 0;

        bool initModel(const std::map<std::string, std::string> &model_config) override
        {
            isAvailable_ = true;
            return true;
        }
        bool isAvailable() override { return isAvailable_; }
        std::string getModelName() const override { return "fake-tool"; }
        std::string getModelDesc() const override { return "测试用假 provider"; }
        chat_sdk::LLMResponse sendMessage(const std::vector<chat_sdk::Message> &messages,
                                          const std::map<std::string, std::string> &request_param,
                                          const std::vector<chat_sdk::ToolDefinition> &tools) override
        {
            chat_sdk::LLMResponse resp;
            call_count++;
            if (call_count == 1)
            {
                resp.tool_calls.push_back(chat_sdk::ToolCall("call_1", "get_weather", "{\"city\":\"北京\"}"));
            }
            else
            {
                resp.content = "北京今天晴,25度";
            }
            resp.success = true;
            return resp;
        }
        std::string sendMessageStream(const std::vector<chat_sdk::Message> &messages,
                                      const std::map<std::string, std::string> &request_param,
                                      func_stream callback) override
        {
            return "";
        }
    };

    class FakeWeatherTool : public chat_sdk::Tool
    {
    public:
        std::string name() const override { return "get_weather"; }
        std::string description() const override { return "查询指定城市天气"; }
        std::vector<chat_sdk::ToolParameter> parameters() const override
        {
            return {chat_sdk::ToolParameter("city", "string", "城市名", true)};
        }
        std::string execute(const std::string &arguments_json) override
        {
            return "{\"weather\":\"晴\",\"temp\":25}";
        }
    };
}

TEST(ChatSDKTest, functionCallingLoop)
{
    // 注册假 provider 类型(插件化注册的注入点: 无网络的端到端测试)
    chat_sdk::ProviderFactory::instance().registerProvider(
        "fake-tool", []() { return std::make_unique<FakeToolProvider>(); });

    auto sdk = std::make_shared<chat_sdk::ChatSDK>();
    const std::string json = R"(
    {
      "models": [
        { "name": "fake-model", "provider": "fake-tool", "api_key": "x", "base_url": "http://fake" }
      ]
    })";
    ASSERT_TRUE(sdk->initFromConfigString(json));

    // 注册可执行工具
    auto tool = std::make_shared<FakeWeatherTool>();
    sdk->registerTool(tool);
    EXPECT_EQ(sdk->getToolNames().size(), 1u);

    auto session_id = sdk->createSession("fake-model");
    ASSERT_FALSE(session_id.empty());

    // 触发工具调用循环: 第一次返回 tool_calls,工具被执行并回填,第二次返回最终答案
    std::string reply = sdk->sendMessage(session_id, "北京天气怎么样?");
    EXPECT_EQ(reply, "北京今天晴,25度");

    // 验证工具确实被调用过
    EXPECT_EQ(tool->name(), "get_weather");
}

int main(int argc, char *argv[])
{
    Logger::instance().init(false, "logs/log.log", spdlog::level::trace);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}