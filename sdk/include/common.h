/**
 *@file common.h
 *
 *@author yui
 */

// 封装公共配置和描述信息，如：模型名称、温度值、最大tokens数、apikey等

/*
curl https://api.deepseek.com/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer ${DEEPSEEK_API_KEY}" \
  -d '{
        "model": "deepseek-chat",
        "messages": [
          {"role": "system", "content": "You are a helpful assistant."},
          {"role": "user", "content": "Hello!"}
        ],
        "stream": false
      }'
*/

#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <chrono>

namespace chat_sdk
{
    // 消息结构
    struct Message
    {
        std::string id;        // 消息标识符
        std::string role;      // user / system
        std::string content;   // 消息内容
        std::time_t timestamp; // 消息生成时间
        Message(const std::string &r, const std::string &con)
            : role(r),
              content(con),
              timestamp(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))
        {
        }
    };

    // 会话结构
    struct Session
    {
        std::string id;
        std::string model_name;
        std::vector<Message> messages;
        std::time_t create_at;
        std::time_t updated_at;
        Session(const std::string &model)
            : model_name(model),
              create_at(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())),
              updated_at(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()))
        {
        }
        void update(std::time_t &time)
        {
            updated_at = time;
        }
    };

    // 调用模型时配置信息,本地
    struct Config
    {
        std::string model_name;
        double temperture = 0.7; // 采样温度
        int max_tokens = 2048;   // 最大token数
        virtual ~Config() = default;
    };

    // API配置结构，api调用
    struct ApiConfig : public Config
    {
        std::string api_key;
    };

    // LLM模型信息
    struct ModelInfo
    {
        std::string name_;
        std::string desc_;
        std::string provider_; // 模型提供者
        std::string endpoint_; // 模型base url
        bool isInit_;          // 模型是否初始化
        ModelInfo(const std::string &name, const std::string &desc = "", const std::string &provider = "", const std::string endpoint = "")
            : name_(name),
              desc_(desc),
              provider_(provider),
              endpoint_(endpoint),
              isInit_(false)
        {
        }
    };

}

#endif