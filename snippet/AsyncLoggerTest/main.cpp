#include "logmanager.h"


int main()
{
    auto &manager = dlog::LogManager::GetInstance();
    auto file = dlog::AsyncLogger::BuildAsyncLogger<dlog::FileFlush>("logs/async-logger.log");
    manager.registerLogger("file", file);

    manager.getDefaultLogger()->message(__FILE__, __LINE__, dlog::LogLevel::INFO).stream() << "console message";
    manager.getLogger("file")->message(__FILE__, __LINE__, dlog::LogLevel::INFO).stream() << "file message";

    // 未注册的日志器由最后一个持有者自动回收。
    auto rolling = dlog::AsyncLogger::BuildAsyncLogger<dlog::RollFileFlush>("logs/async-rolling", 1024 * 1024);
    rolling->message(__FILE__, __LINE__, dlog::LogLevel::INFO).stream() << "rolling message";
}
