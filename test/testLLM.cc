#include <gtest/gtest.h>
#include "../sdk/include/logger.h"
#include "../sdk/include/DeepSeekProvider.h"

TEST(DeepSeekProviderTest, sendMessageDeepSeek)
{
    std::map<std::string, std::string> param_map;
    // param_map["api_key"] = std::getenv("deepseek_apikey");
    param_map["api_key"] = "sk-167814e7c5b04883b4210d3aaa063cca";
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
    // param_map["api_key"] = std::getenv("deepseek_apikey");
    param_map["api_key"] = "sk-167814e7c5b04883b4210d3aaa063cca";
    param_map["temperature"] = "0.7";
    param_map["base_url"] = "https://api.deepseek.com";
    auto deepseekProvider = std::make_shared<chat_sdk::DeepSeekProvider>();
    ASSERT_TRUE(deepseekProvider != nullptr);
    deepseekProvider->initModel(param_map);
    ASSERT_TRUE(deepseekProvider->isAvailable());

    std::vector<chat_sdk::Message> messages;
    messages.push_back({"system","You are a helpful assistant."});
    messages.push_back({"user", "什么是SSE"});
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

int main(int argc, char *argv[])
{
    Logger::instance().init(false, "logs/log.log", spdlog::level::trace);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}