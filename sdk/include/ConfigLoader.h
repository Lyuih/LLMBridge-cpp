/**
 * @file ConfigLoader.h
 * @author yui
 */

// 从 JSON 配置文件加载模型配置,支持配置驱动初始化(无需改代码接入新模型)

#ifndef CONFIGLOADER_H
#define CONFIGLOADER_H

#include <memory>
#include <string>
#include <vector>
#include "common.h"

namespace chat_sdk
{
    class ConfigLoader
    {
    public:
        // 从 JSON 文件加载模型配置列表
        // 失败时返回空 vector 并通过 error 输出原因
        static std::vector<std::shared_ptr<Config>> loadFromFile(const std::string &path, std::string &error);

        // 从 JSON 字符串加载模型配置列表(便于单元测试)
        static std::vector<std::shared_ptr<Config>> loadFromString(const std::string &json_str, std::string &error);
    };
}

#endif