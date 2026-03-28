/**
 * @file LLMManager.h
 * @author yui
 *
 */

#ifndef LLMMANAGER_H
#define LLMMANAGER_H

#include <map>
#include <memory>
#include "common.h"
#include "LLMProvider.h"
#include "logger.h"

namespace chat_sdk
{
    class LLMManager
    {
    public:
        // 注册LLM提供者
        bool registerProvider(const std::string &model_name, std::unique_ptr<LLMProvider> provider);
        // 初始化制定模型
        bool initModel(const std::string& model_name, const std::map<std::string, std::string> &model_param);
        // 检查模型是否可用
        bool isModelAvilable(const std::string &model_name) const;
        // 发送消息给指定模型
        std::string sendMessage(const std::string &model_name, const std::vector<Message> &messages,
                                const std::map<std::string, std::string> &request_param);
        // 发送消息流给指定模型
        std::string sendMessageStream(const std::string &model_name, const std::vector<Message> &messages, const std::map<std::string, std::string> &request_param, LLMProvider::func_stream &callback);

    private:
        // 项目名称 -> 模型
        std::map<std::string, std::unique_ptr<LLMProvider>> providers_;
        // 模型名称->模型描述
        std::map<std::string, ModelInfo> modelInfo_;
    };
}
#endif