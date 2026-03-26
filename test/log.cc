#include "../sdk/include/logger.h"

int main()
{
    // Logger::instance().init(false,"",spdlog::level::info);
    Logger::instance().init(true, "logs/app.log", spdlog::level::info);
    LOG_DEBUG("test_1");
    LOG_DEBUG("test_{}",2);
    LOG_DEBUG("test_{}","123");
    return 0;
}