#include "log.h"


int main()
{
    auto &manager = dlog::LogManager::GetInstance();
    auto file = manager.buildLogger<dlog::FileFlush>("file", "logs/async-logger.log");

    DLOG_INFO() << "console message";
    DLOG_LOGGER_INFO(manager.getLogger("file")) << "file message";

    // 所有日志器由 Manager 统一管理。
    auto rolling = manager.buildLogger<dlog::RollFileFlush>("rolling", "logs/async-rolling", 1024 * 1024);
    DLOG_LOGGER_INFO(rolling) << "rolling message";
}
