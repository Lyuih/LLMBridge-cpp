#include "../include/LLMManager.h"
#include "../include/logger.h"
#include "../include/fields.h"

namespace chat_sdk
{
    // 注册LLM提供者
    bool LLMManager::registerProvider(const std::string &model_name, std::unique_ptr<LLMProvider> provider)
    {
        if (!provider)
        {
            LOG_ERROR("不能传入nullptr {}", model_name);
            return false;
        }
        providers_[model_name] = std::move(provider);
        modelInfo_[model_name] = ModelInfo(model_name);
        LOG_INFO("添加LL提供者成功:{}", model_name);
        return true;
    }
    // 初始化制定模型
    bool LLMManager::initModel(const std::string &model_name, const std::map<std::string, std::string> &model_param)
    {
        auto it = providers_.find(model_name);
        if (it == providers_.end())
        {
            LOG_ERROR("模型未注册:{}", model_name);
            return false;
        }
        bool isSuccess = it->second->initModel(model_param);
        if (!isSuccess)
        {
            LOG_ERROR("{}模型初始化失败", model_name);
        }
        else
        {
            LOG_INFO("{}模型初始化成功", model_name);
            modelInfo_[model_name].desc_ = it->second->getModelDesc();
            modelInfo_[model_name].isInit_ = true;
        }
        return isSuccess;
    }
    // 检查模型是否可用
    bool LLMManager::isModelAvilable(const std::string &model_name) const
    {
        if(providers_.empty())
        {
            LOG_WARN("当前没有模型提供者");
            return false;
        }
        auto it = providers_.find(model_name);
        if (it == providers_.end())
        {
            LOG_ERROR("模型未注册:{}", model_name);
            return false;
        }
        return it->second->isAvailable();
    }
    // 获取所有可以模型列表
    std::vector<ModelInfo> LLMManager::getAvailableModel()
    {
        std::vector<ModelInfo> model_infos;
        for (const auto &pair : modelInfo_)
        {
            if (pair.second.isInit_)
            {
                model_infos.push_back(pair.second);
            }
        }
        return model_infos;
    }
    // 发送消息给指定模型
    std::string LLMManager::sendMessage(const std::string &model_name, const std::vector<Message> &messages,
                                        const std::map<std::string, std::string> &request_param)
    {
        auto it = providers_.find(model_name);
        if (it == providers_.end())
        {
            LOG_ERROR("模型未注册:{}", model_name);
            return "";
        }
        if (!isModelAvilable(model_name))
        {
            LOG_ERROR("{}模型不可用", model_name);
            return "";
        }
        return it->second->sendMessage(messages, request_param);
    }
    // 发送消息流给指定模型
    std::string LLMManager::sendMessageStream(const std::string &model_name, const std::vector<Message> &messages, const std::map<std::string, std::string> &request_param,const LLMProvider::func_stream &call_back)
    {
        auto it = providers_.find(model_name);
        if (it == providers_.end())
        {
            LOG_ERROR("模型未注册:{}", model_name);
            return "";
        }
        if (!isModelAvilable(model_name))
        {
            LOG_ERROR("{}模型不可用", model_name);
            return "";
        }
        return it->second->sendMessageStream(messages, request_param, call_back);
    }

}