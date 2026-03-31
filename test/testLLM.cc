#include <gtest/gtest.h>
#include "../sdk/include/logger.h"
#include "../sdk/include/DeepSeekProvider.h"
#include "../sdk/include/GeminiProvider.h"
#include "../sdk/include/GPTProvider.h"
#include "../sdk/include/LLMManager.h"
#include "../sdk/include/ChatSDK.h"
#include "../sdk/include/common.h"

#if 0
TEST(DeepSeekProviderTest, sendMessageDeepSeek)
{
    std::map<std::string, std::string> param_map;
    param_map["api_key"] = std::getenv("deepseek_apikey");
    param_map["temperature"] = "0.7";
    param_map["base_url"] = "https://api.deepseek.com";
    auto deepseekProvider = std::make_shared<chat_sdk::DeepSeekProvider>();
    ASSERT_TRUE(deepseekProvider != nullptr);
    deepseekProvider->initModel(param_map);
    ASSERT_TRUE(deepseekProvider->isAvailable());

    std::vector<chat_sdk::Message> messages;
    messages.push_back({"user", "你好"});
    std::string response = deepseekProvider->sendMessage(messages, param_map);
    ASSERT_FALSE(response.empty());
    LOG_INFO("response:{}", response);
}

TEST(DeepSeekProviderTest, sendMessageStreamDeepSeek)
{
    std::map<std::string, std::string> param_map;
    param_map["api_key"] = std::getenv("deepseek_apikey");
    param_map["temperature"] = "0.7";
    param_map["base_url"] = "https://api.deepseek.com";
    auto deepseekProvider = std::make_shared<chat_sdk::DeepSeekProvider>();
    ASSERT_TRUE(deepseekProvider != nullptr);
    deepseekProvider->initModel(param_map);
    ASSERT_TRUE(deepseekProvider->isAvailable());

    std::vector<chat_sdk::Message> messages;
    messages.push_back({"system", "You are a helpful assistant."});
    messages.push_back({"user", "你好"});
    std::string response = deepseekProvider->sendMessageStream(messages, param_map, [&](const std::string &chunk, bool last)
                                                               {
        LOG_INFO("chunk: {}",chunk);
        if(last)
        {
            LOG_INFO("[DONE]");
        } });
    ASSERT_FALSE(response.empty());
    LOG_INFO("response:{}", response);
}


TEST(GeminiProviderTest, sendMessageGemini)
{
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::map<std::string, std::string> param_map;
    const char *api_key_env = std::getenv("gemini_api");

    if (api_key_env == nullptr)
    {
        std::cerr << "错误：未设置环境变量 gemini_api！" << std::endl;
        return; // 直接退出，避免崩溃
    }

    // 确认非空后，再赋值
    param_map["api_key"] = api_key_env;
    param_map["temperature"] = "0.7";
    param_map["base_url"] = "https://generativelanguage.googleapis.com";
    auto geminiProvider = std::make_shared<chat_sdk::GeminiProvider>();
    ASSERT_TRUE(geminiProvider != nullptr);
    geminiProvider->initModel(param_map);
    ASSERT_TRUE(geminiProvider->isAvailable());

    std::vector<chat_sdk::Message> messages;
    messages.push_back({"user", "你好"});
    std::string response = geminiProvider->sendMessage(messages, param_map);
    ASSERT_FALSE(response.empty());
    LOG_INFO("response:{}", response);
}

TEST(GeminiProviderTest, sendMessageStreamGemini)
{
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::map<std::string, std::string> param_map;
    const char *api_key_env = std::getenv("gemini_api");

    if (api_key_env == nullptr)
    {
        std::cerr << "错误：未设置环境变量 gemini_api！" << std::endl;
        return; // 直接退出，避免崩溃
    }

    // 确认非空后，再赋值
    param_map["api_key"] = api_key_env;
    param_map["temperature"] = "0.7";
    param_map["base_url"] = "https://generativelanguage.googleapis.com";
    auto geminiProvider = std::make_shared<chat_sdk::GeminiProvider>();
    ASSERT_TRUE(geminiProvider != nullptr);
    geminiProvider->initModel(param_map);
    ASSERT_TRUE(geminiProvider->isAvailable());

    std::vector<chat_sdk::Message> messages;
    messages.push_back({"system", "You are a helpful assistant."});
    messages.push_back({"user", "你好"});
    std::string response = geminiProvider->sendMessageStream(messages, param_map, [&](const std::string &chunk, bool last)
                                                             {
        LOG_INFO("chunk: {}",chunk);
        if(last)
        {
            LOG_INFO("[DONE]");
        } });
    ASSERT_FALSE(response.empty());
    LOG_INFO("response:{}", response);
}



TEST(GPTProviderTest, sendMessageGPT)
{
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::map<std::string, std::string> param_map;
    const char *api_key_env = std::getenv("GPT_apikey");

    if (api_key_env == nullptr)
    {
        std::cerr << "错误：未设置环境变量 GPT_apikey！" << std::endl;
        return; // 直接退出，避免崩溃
    }

    // 确认非空后，再赋值
    param_map["api_key"] = api_key_env;
    param_map["temperature"] = "0.7";
    param_map["base_url"] = "https://api.openai.com";
    auto GptProvider = std::make_shared<chat_sdk::GPTProvider>();
    ASSERT_TRUE(GptProvider != nullptr);
    GptProvider->initModel(param_map);
    ASSERT_TRUE(GptProvider->isAvailable());

    std::vector<chat_sdk::Message> messages;
    messages.push_back({"user", "你好"});
    std::string response = GptProvider->sendMessage(messages, param_map);
    ASSERT_FALSE(response.empty());
    LOG_INFO("response:{}", response);
}

TEST(LLMManagerTest, ProviderTest)
{
    std::map<std::string, std::string> param_map;
    const char *api_key_env = std::getenv("deepseek_apikey");

    if (api_key_env == nullptr)
    {
        std::cerr << "错误：未设置环境变量 deepseek_apikey！" << std::endl;
        return; // 直接退出，避免崩溃
    }

    // 确认非空后，再赋值
    param_map["api_key"] = api_key_env;
    param_map["temperature"] = "0.7";
    param_map["base_url"] = "https://api.deepseek.com";
    auto deepseek_Provider = std::make_unique<chat_sdk::DeepSeekProvider>();
    chat_sdk::LLMManager manager;
    manager.registerProvider("deepseek-chat", std::move(deepseek_Provider));
    manager.initModel("deepseek-chat", param_map);
    // ASSERT_TRUE(GptProvider != nullptr);
    // GptProvider->initModel(param_map);
    ASSERT_TRUE(manager.isModelAvilable("deepseek-chat"));

    std::vector<chat_sdk::Message> messages;
    messages.push_back({"user", "你好"});
    std::string response = manager.sendMessage("deepseek-chat", messages, param_map);
    ASSERT_FALSE(response.empty());
    // LOG_INFO("response:{}", response);
}
#endif

TEST(ChatSDKTest, ChatSDKInitTest)
{
    auto sdk = std::make_shared<chat_sdk::ChatSDK>();
    ASSERT_TRUE(sdk != nullptr);

    // 配置支持的模型参数,云模型-deepseek gpt gemini
    auto deepseek_config = std::make_shared<chat_sdk::ApiConfig>();
    ASSERT_TRUE(deepseek_config != nullptr);
    deepseek_config->model_name = "deepseek-chat";
    const char *deepseek_apikey = getenv("deepseek_apikey");
    ASSERT_TRUE(deepseek_apikey != nullptr);
    deepseek_config->api_key = deepseek_apikey;
    deepseek_config->endPoint_ = "https://api.deepseek.com";

    // gpt TODO_

    // gemini

    auto gemini_config = std::make_shared<chat_sdk::ApiConfig>();
    ASSERT_TRUE(gemini_config != nullptr);

    gemini_config->model_name = "gemini-2.0-flash";
    const char *gemini_apikey = getenv("gemini_apikey");
    ASSERT_TRUE(gemini_apikey != nullptr);
    gemini_config->api_key = gemini_apikey;
    gemini_config->endPoint_ = "https://generativelanguage.googleapis.com";

    // 加入sdk
    std::vector<std::shared_ptr<chat_sdk::Config>> configs{deepseek_config, gemini_config};

    ASSERT_TRUE(sdk->initModels(configs));

    // 创建会话
    auto session_id = sdk->createSession(deepseek_config->model_name);
    ASSERT_FALSE(session_id.empty());

    std::string message = "你好";
    // std::cout << ">>>";
    // std::getline(std::cin, message);
    auto response = sdk->sendMessage(session_id, message);
    ASSERT_FALSE(response.empty());

    // 获取会话历史消息
    auto messages = sdk->sessionManager_.getSessionHistory(session_id);
    for (const auto &msg : messages)
    {
        std::cout << msg.role << ": " << msg.content << std::endl;
    }
    ASSERT_FALSE(messages.empty());
}

int main(int argc, char *argv[])
{
    Logger::instance().init(false, "logs/log.log", spdlog::level::trace);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}