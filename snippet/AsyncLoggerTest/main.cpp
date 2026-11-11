#include "log.h"


int main()
{
    auto &manager = dlog::LogManager::GetInstance();
    auto file = dlog::AsyncLogger::BuildAsyncLogger<dlog::FileFlush>("logs/async-logger.log");
    manager.registerLogger("file", file);

    DLOG_INFO() << "console message";
    DLOG_LOGGER_INFO(manager.getLogger("file")) << "file message";

    // 未注册的日志器由最后一个持有者自动回收。
    auto rolling = dlog::AsyncLogger::BuildAsyncLogger<dlog::RollFileFlush>("logs/async-rolling", 1024 * 1024);
    DLOG_LOGGER_INFO(rolling) << "rolling message";
}
