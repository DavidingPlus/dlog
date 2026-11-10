#include <gtest/gtest.h>

#include "logmanager.h"

#include <atomic>
#include <cstdlib>
#include <stdexcept>
#include <string>


using namespace dlog;


namespace
{

    class RecordingFlush : public LogFlush
    {
    public:

        explicit RecordingFlush(std::string &text) : m_text(text) {}
        void writeAndFlush(const char *data, size_t len) override { m_text.append(data, len); }

    private:

        std::string &m_text;
    };

    struct OutputError
    {
    };

    class FailingFlush : public LogFlush
    {
    public:

        explicit FailingFlush(std::atomic_int &calls) : m_calls(calls) {}
        void writeAndFlush(const char *, size_t) override
        {
            ++m_calls;
            throw OutputError{};
        }

    private:

        std::atomic_int &m_calls;
    };

} // namespace


static void verifyRegistrationAndShutdown()
{
    static std::string firstText;
    static std::string secondText;
    // LIFO 顺序让验证在 LogManager 的退出处理完成后执行。
    const auto verifyOutput = []
    {
        const auto consoleText = ::testing::internal::GetCapturedStdout();
        EXPECT_NE(consoleText.find("[app.cpp:1] console record\n"), std::string::npos);
        EXPECT_NE(firstText.find("[app.cpp:2] first record\n"), std::string::npos);
        EXPECT_NE(secondText.find("[app.cpp:3] second record\n"), std::string::npos);
        EXPECT_EQ(firstText.find("second record"), std::string::npos);
        EXPECT_EQ(secondText.find("first record"), std::string::npos);
        std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
    };
    ASSERT_EQ(std::atexit(verifyOutput), 0);

    auto &manager = LogManager::GetInstance();
    EXPECT_EQ(&manager, &LogManager::GetInstance());
    EXPECT_EQ(manager.getLogger("missing"), nullptr);

    auto console = manager.getDefaultLogger();
    EXPECT_NE(console, nullptr);
    auto first = AsyncLogger::BuildAsyncLogger<RecordingFlush>(firstText);
    auto second = AsyncLogger::BuildAsyncLogger<RecordingFlush>(secondText);
    EXPECT_THROW(manager.registerLogger("", first), std::invalid_argument);
    EXPECT_THROW(manager.registerLogger("missing", nullptr), std::invalid_argument);
    manager.registerLogger("console", console);
    manager.registerLogger("first", first);
    manager.registerLogger("first-alias", first);
    manager.registerLogger("second", second);
    EXPECT_THROW(manager.registerLogger("first", second), std::logic_error);
    EXPECT_EQ(manager.getLogger("first"), first);
    EXPECT_EQ(manager.getDefaultLogger(), console);

    ::testing::internal::CaptureStdout();
    console->message("app.cpp", 1, LogLevel::INFO).stream() << "console record";
    manager.getLogger("first")->message("app.cpp", 2, LogLevel::INFO).stream() << "first record";
    manager.getLogger("second")->message("app.cpp", 3, LogLevel::INFO).stream() << "second record";
}


static void verifyShutdownAfterOutputFailure()
{
    static std::atomic_int failures{0};
    static std::string healthyText;
    const auto verifyOutput = []
    {
        EXPECT_EQ(failures.load(), 2);
        EXPECT_NE(healthyText.find("healthy record\n"), std::string::npos);
        std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
    };
    ASSERT_EQ(std::atexit(verifyOutput), 0);

    auto &manager = LogManager::GetInstance();
    auto first = AsyncLogger::BuildAsyncLogger<FailingFlush>(failures);
    auto second = AsyncLogger::BuildAsyncLogger<FailingFlush>(failures);
    auto healthy = AsyncLogger::BuildAsyncLogger<RecordingFlush>(healthyText);
    manager.registerLogger("first", first);
    manager.registerLogger("second", second);
    manager.registerLogger("healthy", healthy);
    first->message("app.cpp", 1, LogLevel::INFO).stream() << "first failure";
    second->message("app.cpp", 2, LogLevel::INFO).stream() << "second failure";

    healthy->message("app.cpp", 3, LogLevel::INFO).stream() << "healthy record";
}


// 用独立进程验证正常退出时的自动排空。
TEST(LogManagerDeathTest, RegistersLoggersAndDrainsTheirOwnOutputsAtExit)
{
    EXPECT_EXIT(
        {
            verifyRegistrationAndShutdown();
            std::exit(EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}


TEST(LogManagerDeathTest, ShutdownStillStopsEveryLoggerWhenOutputThrows)
{
    EXPECT_EXIT(
        {
            verifyShutdownAfterOutputFailure();
            std::exit(EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "dlog: LogManager shutdown failed");
}
