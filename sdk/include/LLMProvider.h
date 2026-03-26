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

namespace chat_sdk
{

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
        //发送消息给模型
        virtual std::string sendMessage(const std::vector<Message>&messages,
            const std::map<std::string,std::string>&request_param) = 0;
        //发送消息给模型 流式响应(每生成几个字符就触发回调函数)
        virtual std::string sendMessageStream(const std::vector<Message>&messages,
            const std::map<std::string,std::string>&request_param,func_stream callback) = 0;
    };
}
#endif