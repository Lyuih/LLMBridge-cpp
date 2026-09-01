/**
 * @file cli_demo.cc
 * 命令行交互 demo: 演示 LLMBridge SDK 的真实使用
 * - 配置驱动启动: 读取 JSON 配置文件初始化任意模型
 * - 多模型对话: 通过 /model 切换会话模型
 * - 可观测性: /metrics 查看请求耗时 / token / 错误率
 * - Function Calling: 注册计算器与时间工具,模型可调用
 *
 * 用法: cli_demo [配置文件路径]
 */

#include <iostream>
#include <sstream>
#include <ctime>
#include <json/json.h>
#include "../sdk/include/ChatSDK.h"
#include "../sdk/include/logger.h"
#include "../sdk/include/Tool.h"

using namespace chat_sdk;

namespace
{
    // 示例工具1: 简单计算器(二元运算)
    class CalculatorTool : public Tool
    {
    public:
        std::string name() const override { return "calculator"; }
        std::string description() const override { return "对两个数做二元运算(+ - * /),参数 a,b 为数字,op 为运算符"; }
        std::vector<ToolParameter> parameters() const override
        {
            return {
                ToolParameter("a", "number", "第一个操作数", true),
                ToolParameter("b", "number", "第二个操作数", true),
                ToolParameter("op", "string", "运算符: + - * /", true),
            };
        }
        std::string execute(const std::string &arguments_json) override
        {
            Json::Value args;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream ss(arguments_json);
            if (!Json::parseFromStream(builder, ss, &args, &errs))
            {
                return "{\"error\":\"参数解析失败\"}";
            }
            double a = args.get("a", 0).asDouble();
            double b = args.get("b", 0).asDouble();
            std::string op = args.get("op", "").asString();
            double result = 0;
            bool ok = true;
            std::string err;
            if (op == "+")
            {
                result = a + b;
            }
            else if (op == "-")
            {
                result = a - b;
            }
            else if (op == "*")
            {
                result = a * b;
            }
            else if (op == "/")
            {
                if (b == 0)
                {
                    ok = false;
                    err = "除数不能为0";
                }
                else
                {
                    result = a / b;
                }
            }
            else
            {
                ok = false;
                err = "不支持的运算符: " + op;
            }
            if (!ok)
            {
                return "{\"error\":\"" + err + "\"}";
            }
            return "{\"result\":" + std::to_string(result) + "}";
        }
    };

    // 示例工具2: 获取当前时间
    class TimeTool : public Tool
    {
    public:
        std::string name() const override { return "get_time"; }
        std::string description() const override { return "获取当前时间(格式: YYYY-MM-DD HH:MM:SS),无参数"; }
        std::vector<ToolParameter> parameters() const override { return {}; }
        std::string execute(const std::string &arguments_json) override
        {
            std::time_t now = std::time(nullptr);
            char buf[64];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
            return std::string("{\"time\":\"") + buf + "\"}";
        }
    };
}

int main(int argc, char *argv[])
{
    Logger::instance().init(false, "logs/cli.log", spdlog::level::info);

    const std::string config_path = argc > 1 ? argv[1] : "../config/models.example.json";

    auto sdk = std::make_shared<ChatSDK>();
    std::cout << "===== LLMBridge CLI Demo =====" << std::endl;
    std::cout << "加载配置: " << config_path << std::endl;
    if (!sdk->initFromConfigFile(config_path))
    {
        std::cerr << "初始化失败,请检查配置文件" << std::endl;
        return 1;
    }

    // 注册示例工具
    sdk->registerTool(std::make_shared<CalculatorTool>());
    sdk->registerTool(std::make_shared<TimeTool>());
    std::cout << "已注册工具: calculator, get_time" << std::endl;

    // 可用模型
    auto models = sdk->getAvailableModels();
    std::string current_model;
    if (models.empty())
    {
        std::cerr << "没有可用的模型,请检查配置中的 api_key 与 base_url" << std::endl;
        return 1;
    }
    std::cout << "可用模型:" << std::endl;
    for (const auto &m : models)
    {
        std::cout << "  - " << m.name_ << (m.isInit_ ? " (已就绪)" : " (未就绪)") << std::endl;
        if (current_model.empty())
        {
            current_model = m.name_;
        }
    }
    std::cout << "当前模型: " << current_model << std::endl;
    std::cout << "输入 /help 查看命令,/exit 退出" << std::endl;

    std::string session_id;
    std::string line;
    while (true)
    {
        std::cout << "\n[" << current_model << "] >>> " << std::flush;
        if (!std::getline(std::cin, line))
        {
            break;
        }
        if (line.empty())
        {
            continue;
        }

        // 命令处理
        if (line == "/exit")
        {
            break;
        }
        else if (line == "/help")
        {
            std::cout << "命令:\n"
                      << "  /models       列出可用模型\n"
                      << "  /model <名>   切换当前模型(需先有对应会话)\n"
                      << "  /metrics      查看调用指标\n"
                      << "  /tools        查看已注册工具\n"
                      << "  /new          开启新会话\n"
                      << "  /exit         退出\n"
                      << "  其他输入      作为消息发送给当前模型\n";
            continue;
        }
        else if (line == "/models")
        {
            for (const auto &m : sdk->getAvailableModels())
            {
                std::cout << "  - " << m.name_ << " : " << m.desc_ << std::endl;
            }
            continue;
        }
        else if (line == "/metrics")
        {
            std::cout << "指标: " << sdk->getMetricsJson() << std::endl;
            continue;
        }
        else if (line == "/tools")
        {
            std::cout << "工具: ";
            for (const auto &t : sdk->getToolNames())
            {
                std::cout << t << " ";
            }
            std::cout << std::endl;
            continue;
        }
        else if (line == "/new")
        {
            session_id = sdk->createSession(current_model);
            std::cout << "新会话已创建: " << session_id << std::endl;
            continue;
        }
        else if (line.rfind("/model ", 0) == 0)
        {
            std::string model_name = line.substr(7);
            if (model_name.empty())
            {
                std::cout << "用法: /model <模型名>" << std::endl;
                continue;
            }
            current_model = model_name;
            session_id = sdk->createSession(current_model);
            std::cout << "已切换到模型: " << current_model << ", 新会话: " << session_id << std::endl;
            continue;
        }

        // 发送消息
        if (session_id.empty())
        {
            session_id = sdk->createSession(current_model);
            std::cout << "新会话已创建: " << session_id << std::endl;
        }
        std::cout << "请求中..." << std::endl;
        std::string reply = sdk->sendMessage(session_id, line);
        if (reply.empty())
        {
            std::cout << "(模型无有效回复,请检查日志)" << std::endl;
        }
        else
        {
            std::cout << "\n" << reply << std::endl;
        }
    }

    std::cout << "\n===== 会话结束 =====" << std::endl;
    std::cout << "最终指标: " << sdk->getMetricsJson() << std::endl;
    return 0;
}
