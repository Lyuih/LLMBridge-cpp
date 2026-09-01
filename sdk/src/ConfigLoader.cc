#include <fstream>
#include <sstream>
#include <json/json.h>
#include "../include/ConfigLoader.h"
#include "../include/logger.h"

namespace chat_sdk
{
    namespace
    {
        // 从 Json::Value 读取可选字段,不存在或类型不符时返回默认值
        std::string optString(const Json::Value &obj, const char *key, const std::string &def = "")
        {
            return (obj.isMember(key) && obj[key].isString()) ? obj[key].asString() : def;
        }

        double optDouble(const Json::Value &obj, const char *key, double def)
        {
            return (obj.isMember(key) && obj[key].isNumeric()) ? obj[key].asDouble() : def;
        }

        int optInt(const Json::Value &obj, const char *key, int def)
        {
            return (obj.isMember(key) && obj[key].isNumeric()) ? obj[key].asInt() : def;
        }

        // 单个模型 JSON 对象 → Config
        std::shared_ptr<Config> parseModel(const Json::Value &item, std::string &error)
        {
            const std::string name = optString(item, "name");
            if (name.empty())
            {
                error = "模型缺少 name 字段";
                return nullptr;
            }
            const std::string provider = optString(item, "provider");
            if (provider.empty())
            {
                error = "模型[" + name + "]缺少 provider 字段";
                return nullptr;
            }

            std::shared_ptr<Config> config;
            if (provider == "ollama")
            {
                auto c = std::make_shared<OllamaConfig>();
                c->model_desc_ = optString(item, "desc");
                c->endPoint_ = optString(item, "base_url");
                config = c;
            }
            else
            {
                auto c = std::make_shared<ApiConfig>();
                c->api_key = optString(item, "api_key");
                c->model_desc_ = optString(item, "desc");
                c->endPoint_ = optString(item, "base_url");
                config = c;
            }

            config->model_name = name;
            config->provider_type = provider;
            config->temperature = optDouble(item, "temperature", config->temperature);
            config->max_tokens = optInt(item, "max_tokens", config->max_tokens);
            config->weight = optInt(item, "weight", config->weight);
            config->context_window = optInt(item, "context_window", config->context_window);

            // fallback 备选模型列表
            if (item.isMember("fallback") && item["fallback"].isArray())
            {
                for (const auto &fb : item["fallback"])
                {
                    if (fb.isString() && !fb.asString().empty())
                    {
                        config->fallback.push_back(fb.asString());
                    }
                }
            }
            // route 虚拟路由组
            if (item.isMember("route") && item["route"].isArray())
            {
                for (const auto &r : item["route"])
                {
                    if (r.isString() && !r.asString().empty())
                    {
                        config->route.push_back(r.asString());
                    }
                }
            }
            return config;
        }
    }

    std::vector<std::shared_ptr<Config>> ConfigLoader::loadFromFile(const std::string &path, std::string &error)
    {
        std::ifstream in(path);
        if (!in.is_open())
        {
            error = "无法打开配置文件: " + path;
            LOG_ERROR("{}", error);
            return {};
        }
        std::stringstream buffer;
        buffer << in.rdbuf();
        return loadFromString(buffer.str(), error);
    }

    std::vector<std::shared_ptr<Config>> ConfigLoader::loadFromString(const std::string &json_str, std::string &error)
    {
        Json::Value root;
        Json::CharReaderBuilder readBuilder;
        std::string errs;
        std::istringstream json_stream(json_str);
        if (!Json::parseFromStream(readBuilder, json_stream, &root, &errs))
        {
            error = "配置 JSON 解析失败: " + errs;
            LOG_ERROR("{}", error);
            return {};
        }

        if (!root.isMember("models") || !root["models"].isArray())
        {
            error = "配置缺少 models 数组";
            LOG_ERROR("{}", error);
            return {};
        }

        std::vector<std::shared_ptr<Config>> configs;
        const Json::Value &models = root["models"];
        for (unsigned int i = 0; i < models.size(); ++i)
        {
            std::string item_error;
            auto config = parseModel(models[i], item_error);
            if (!config)
            {
                error = "models[" + std::to_string(i) + "] 配置无效: " + item_error;
                LOG_ERROR("{}", error);
                return {};
            }
            configs.push_back(config);
        }

        LOG_INFO("配置加载成功,共 {} 个模型", configs.size());
        return configs;
    }
}
