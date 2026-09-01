/**
 * @file LLMProvider.h
 * @author yui
 */

// 大模型提供者,不同大模型需要继承基类大模型

#ifndef LLMPROVIDER_H
#define LLMPROVIDER_H
#include <string>
#include <unordered_map>
#include <functional>
#include <map>

#include "common.h"
#include "Tool.h"

namespace chat_sdk
{
    // 模型一次调用的结构化结果: 文本 + 工具调用请求 + token 用量
    struct LLMResponse
    {
        std::string content;                    // 文本内容(请求工具时可能为空)
        std::vector<ToolCall> tool_calls;       // 模型请求调用的工具列表
        int input_tokens = 0;                   // 本次调用输入 token
        int output_tokens = 0;                  // 本次调用输出 token
        bool success = false;                   // 是否成功
        std::string error;                      // 失败原因

        bool hasToolCalls() const { return !tool_calls.empty(); }
    };

    class LLMProvider
    {
    protected:
        bool isAvailable_ = false; // 模型是否有效
        std::string api_key_;      // API KEY
        std::string endPoint_;     // base url
    public:
        using func_stream = std::function<void (const std::string&,bool)>;
        //模型初始化
        virtual bool initModel(const std::map<std::string,std::string>&model_config) = 0;
        //检查模型是否有效
        virtual bool isAvailable() = 0;
        //获取模型名称
        virtual std::string getModelName() const = 0;
        //获取描述信息
        virtual std::string getModelDesc() const = 0;
        //发送消息给模型 全量
        //tools 非空时注入工具 schema,支持 Function Calling
        virtual LLMResponse sendMessage(const std::vector<Message>&messages,
            const std::map<std::string,std::string>&request_param,
            const std::vector<ToolDefinition>& tools = {}) = 0;
        //发送消息给模型 流式响应(每生成几个字符就触发回调函数)
        virtual std::string sendMessageStream(const std::vector<Message>&messages,
            const std::map<std::string,std::string>&request_param,func_stream callback) = 0;
    };
}
#endif