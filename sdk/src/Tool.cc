#include "../include/Tool.h"
#include "../include/logger.h"

namespace chat_sdk
{
    void ToolRegistry::registerTool(std::shared_ptr<Tool> tool)
    {
        if (!tool)
        {
            LOG_ERROR("工具为空,无法注册");
            return;
        }
        tools_[tool->name()] = std::move(tool);
        LOG_INFO("注册工具:{}", tools_.rbegin()->first);
    }

    std::string ToolRegistry::execute(const std::string &name, const std::string &arguments_json) const
    {
        auto it = tools_.find(name);
        if (it == tools_.end())
        {
            LOG_ERROR("未知工具:{}", name);
            return "{\"error\":\"unknown tool: " + name + "\"}";
        }
        LOG_INFO("执行工具:{} 参数:{}", name, arguments_json);
        try
        {
            return it->second->execute(arguments_json);
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("工具 {} 执行异常:{}", name, e.what());
            return std::string("{\"error\":\"") + e.what() + "\"}";
        }
    }

    std::vector<ToolDefinition> ToolRegistry::definitions() const
    {
        std::vector<ToolDefinition> defs;
        defs.reserve(tools_.size());
        for (const auto &pair : tools_)
        {
            ToolDefinition def;
            def.name = pair.second->name();
            def.description = pair.second->description();
            def.parameters = pair.second->parameters();
            defs.push_back(std::move(def));
        }
        return defs;
    }

    std::vector<std::string> ToolRegistry::names() const
    {
        std::vector<std::string> names;
        names.reserve(tools_.size());
        for (const auto &pair : tools_)
        {
            names.push_back(pair.first);
        }
        return names;
    }

    bool ToolRegistry::empty() const
    {
        return tools_.empty();
    }
}
