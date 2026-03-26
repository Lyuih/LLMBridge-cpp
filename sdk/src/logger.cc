#include "../include/logger.h"

// 将所有重量级的内部依赖全部移到 cpp 文件中
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/async.h>
#include <vector>
#include <cstdlib>

// ==================== 内部配置项 ====================
namespace {
    // 替代宏，采用 C++ 原生 constexpr 提升类型安全，也不污染外部命名空间
    constexpr const char* LOG_PATTERN = "[%Y-%m-%d %H:%M:%S.%e][%^%l%$] [%@] %v";
    constexpr size_t ASYNC_QUEUE_SIZE = 8192; // 注意：必须是 2 的幂次方
    constexpr size_t ASYNC_THREAD_COUNT = 1;
}

Logger &Logger::instance()
{
    static Logger logger_instance;
    return logger_instance;
}

Logger::Logger()
{
    // 默认空logger，防止在未初始化时调用引发崩溃
    logger_ = spdlog::null_logger_mt("default_null_logger");
}

Logger::~Logger()
{
    // 析构时安全关闭并刷盘
    spdlog::shutdown();
}

void Logger::init(bool release_mode, const std::string &file_name, spdlog::level::level_enum level)
{
    std::call_once(init_flag_, [&]() {
        try {
            // 初始化全局异步线程池
            spdlog::init_thread_pool(ASYNC_QUEUE_SIZE, ASYNC_THREAD_COUNT);
            std::vector<spdlog::sink_ptr> sinks;

            // 1. 控制台 Sink
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(level);
            sinks.push_back(console_sink);

            // 2. 文件 Sink (Release模式启用)
            if (release_mode) {
                // 0点0分自动切割文件
                auto daily_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(file_name, 0, 0); 
                daily_sink->set_level(level);
                sinks.push_back(daily_sink);
            }

            // 3. 创建异步 logger，采用满了即阻塞的策略(防止丢日志)
            logger_ = std::make_shared<spdlog::async_logger>(
                "application_logger",
                sinks.begin(), sinks.end(),
                spdlog::thread_pool(),
                spdlog::async_overflow_policy::block);

            logger_->set_level(level);
            logger_->set_pattern(LOG_PATTERN);
            
            // 遇到 ERROR 或以上级别日志时，立即将缓冲区刷入文件，防止崩溃丢现场
            logger_->flush_on(spdlog::level::err);
            
            // 定时刷新：每 3 秒后台自动将缓存刷入磁盘，兼顾性能与实时性
            spdlog::flush_every(std::chrono::seconds(3));

            spdlog::set_default_logger(logger_);

        } catch (const spdlog::spdlog_ex &ex) {
            fprintf(stderr, "日志初始化失败: %s\n", ex.what());
        }
    });
}