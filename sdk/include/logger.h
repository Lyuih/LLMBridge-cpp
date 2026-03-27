#ifndef LOGGER_H
#define LOGGER_H
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE 
#include <spdlog/spdlog.h> // 需要 spdlog 的日志宏和 level 枚举
#include <memory>
#include <mutex>
#include <string>

class Logger
{
public:
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // 获取单例
    static Logger &instance();

    // 初始化日志
    // release_mode: false=Debug(控制台), true=Release(控制台+按日文件)
    // file_name: 日志文件路径
    // level: 日志输出级别
    void init(bool release_mode, const std::string &file_name, spdlog::level::level_enum level);

    // 获取 logger（保留为内联，提升宏展开后的执行效率）
    inline std::shared_ptr<spdlog::logger> get_logger() const
    {
        return logger_;
    }

private:
    Logger();
    ~Logger();

    std::shared_ptr<spdlog::logger> logger_;
    std::once_flag init_flag_;
};

// ==================== 跨平台日志宏 ====================

// 建议将 LOG_ 修改为项目前缀，如 MYAPP_LOG_INFO 防止冲突
#define LOG_TRACE(...)  SPDLOG_LOGGER_TRACE(Logger::instance().get_logger(), __VA_ARGS__)
#define LOG_DEBUG(...)  SPDLOG_LOGGER_DEBUG(Logger::instance().get_logger(), __VA_ARGS__)
#define LOG_INFO(...)   SPDLOG_LOGGER_INFO(Logger::instance().get_logger(), __VA_ARGS__)
#define LOG_WARN(...)   SPDLOG_LOGGER_WARN(Logger::instance().get_logger(), __VA_ARGS__)
#define LOG_ERROR(...)  SPDLOG_LOGGER_ERROR(Logger::instance().get_logger(), __VA_ARGS__)

#define LOG_FATAL(...)  do { \
    SPDLOG_LOGGER_CRITICAL(Logger::instance().get_logger(), __VA_ARGS__); \
    std::abort(); \
} while(0)

#endif // LOGGER_H