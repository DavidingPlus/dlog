#include "asynclogging.h"
#include "logger.h"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <string>


namespace
{
    constexpr int64_t kRollSize = 1LL * 1024 * 1024;

    AsyncLogging *g_asyncLog = nullptr;


    void asyncLog(const char *message, size_t length)
    {
        if (g_asyncLog) g_asyncLog->append(message, length);
    }

    void testLogging()
    {
        DLOG_DEBUG() << "debug";
        DLOG_INFO() << "info";
        DLOG_WARN() << "warn";
        DLOG_ERROR() << "error";
        DLOG_SYS_ERROR(ENOENT);

        for (int i = 0; i < 10; ++i) DLOG_INFO() << "Hello, " << i << " abc...xyz";
    }

    void testAsyncLogging()
    {
        for (int i = 0; i < 1024; ++i) DLOG_INFO() << "Hello, " << i << " abc...xyz";
    }

} // namespace


int main()
{
    std::filesystem::path logDirectory("logs");
    std::filesystem::create_directories(logDirectory);

    std::string logBasePath = (logDirectory / "AsyncLoggingTest").string();
    AsyncLogging logging(logBasePath, kRollSize);

    // 先使用 Logger 的默认输出，确认日志格式化与异步输出切换前的行为。
    testLogging();

    // AsyncLogging 对象和回调目标先准备好，再启动后台线程并切换 Logger 输出。
    g_asyncLog = &logging;
    logging.start();
    Logger::SetOutput(asyncLog, LogLevelColorMode::OFF);

    testLogging();
    testAsyncLogging();

    // stop() 会排空生产者缓冲区、写出剩余日志并完成最终 flush。
    logging.stop();
    g_asyncLog = nullptr;
}
