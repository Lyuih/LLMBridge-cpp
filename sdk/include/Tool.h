/**
 * @file Tool.h
 * @author yui
 */

// Function Calling / 工具调用
// - ToolDefinition: 工具 schema,注入模型请求体的 tools 字段
// - Tool: 可执行工具接口,SDK 侧实现并注册
// - ToolRegistry: 工具注册表,负责生成 schema 列表 + 按名执行
// 执行循环由 ChatSDK 驱动: 模型请求工具 -> 执行 -> 结果回填 -> 再次调用模型

#ifndef TOOL_H
#define TOOL_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace chat_sdk
{
    // 工具参数定义(JSON Schema 子集)
    struct ToolParameter
    {
        std::string name;        // 参数名
        std::string type;        // "string" / "integer" / "number" / "boolean"
        std::string description; // 参数说明(帮助模型正确传参)
        bool required = false;   // 是否必填

        ToolParameter() = default;
        ToolParameter(std::string name_, std::string type_, std::string description_, bool required_ = false)
            : name(std::move(name_)), type(std::move(type_)), description(std::move(description_)), required(required_) {}
    };

    // 工具定义,注入请求体的 tools 字段
    struct ToolDefinition
    {
        std::string name;        // 工具名(模型用这个名字调用)
        std::string description; // 工具说明(决定模型何时调用)
        std::vector<ToolParameter> parameters;

        ToolDefinition() = default;
        ToolDefinition(std::string name_, std::string description_, std::vector<ToolParameter> parameters_ = {})
            : name(std::move(name_)), description(std::move(description_)), parameters(std::move(parameters_)) {}
    };

    // 可执行工具接口。SDK 使用者实现并注册。
    class Tool
    {
    public:
        virtual ~Tool() = default;
        // 工具名
        virtual std::string name() const = 0;
        // 工具说明(注入模型提示词)
        virtual std::string description() const = 0;
        // 参数定义
        virtual std::vector<ToolParameter> parameters() const { return {}; }
        // 执行工具,入参为 JSON 字符串,返回执行结果文本
        virtual std::string execute(const std::string &arguments_json) = 0;
    };

    // 工具注册表
    class ToolRegistry
    {
    public:
        // 注册工具(同名覆盖)
        void registerTool(std::shared_ptr<Tool> tool);
        // 按名执行工具,未知工具返回错误文本
        std::string execute(const std::string &name, const std::string &arguments_json) const;
        // 生成注入请求体的工具 schema 列表
        std::vector<ToolDefinition> definitions() const;
        // 工具名列表
        std::vector<std::string> names() const;
        // 是否为空
        bool empty() const;

    private:
        std::map<std::string, std::shared_ptr<Tool>> tools_;
    };
}

#endif