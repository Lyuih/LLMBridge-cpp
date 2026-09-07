/**
 *@file common.h
 *
 *@author yui
 */

// 封装公共配置和描述信息，如：模型名称、温度值、最大tokens数、apikey等

/*
curl https://api.openai.com/v1/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer ${OPENAI_API_KEY}" \
  -d '{
        "model": "gpt-4o-mini",
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
#include <utility>

namespace chat_sdk
{
    // 模型请求的工具调用
    struct ToolCall
    {
        std::string id;        // 调用 id,回填结果时用到
        std::string name;      // 工具名
        std::string arguments; // 参数 JSON 字符串
        ToolCall() = default;
        ToolCall(std::string id_, std::string name_, std::string arguments_)
            : id(std::move(id_)), name(std::move(name_)), arguments(std::move(arguments_)) {}
    };

    // 消息结构
    struct Message
    {
        std::string id;        // 消息标识符
        std::string role;      // system / user / assistant / tool
        std::string content;   // 消息内容(assistant 请求工具时可为空)
        std::string tool_call_id; // role=="tool" 时关联的工具调用 id (OpenAI 格式回填)
        std::string tool_name;    // role=="tool" 时关联的工具名 (Gemini functionResponse 需要)
        std::vector<ToolCall> tool_calls; // role=="assistant" 时模型请求的工具调用列表
        std::time_t timestamp; // 消息生成时间
        Message(const std::string &role_, const std::string &content_)
            : role(role_),
              content(content_),
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
        void update()
        {
            updated_at = messages.back().timestamp;
        }
    };

    // 调用模型时配置信息,本地
    struct Config
    {
        std::string model_name;       // 模型名(会话/路由使用的外部名字)
        std::string provider_type;    // 协议族类型: "openai" / "claude" / "gemini"(可注册自定义类型),驱动工厂创建
        double temperature = 0.7;     // 采样温度
        int max_tokens = 2048;        // 最大token数
        int weight = 1;               // 路由权重,同模型名多provider时按权重分配流量(0 表示不参与路由)
        std::vector<std::string> fallback; // 故障转移备选模型名,主模型失败时按顺序尝试
        std::vector<std::string> route;    // 若非空,则本模型是虚拟路由组,值指向后端模型名(按各自 weight 分配流量)
        int context_window = 0;       // 上下文窗口大小(token),0 表示不限,用于历史裁剪: 预算 = context_window - max_tokens
        virtual ~Config() = default;
    };

    // API配置结构，api调用
    struct ApiConfig : public Config
    {
        std::string api_key;
        std::string model_desc_;
        std::string endPoint_;
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
        ModelInfo() = default;
    };

}

#endif