#include <gtest/gtest.h>

#include "logmanager.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
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

    class DiagnosticFlush : public LogFlush
    {
    public:

        void writeAndFlush(const char *, size_t) override { throw std::runtime_error("output diagnostic"); }
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


static void verifyFileAtExit(bool explicitStop)
{
    const char *existing = std::getenv("DLOG_MANAGER_EXIT_TEST_FILE");
    const bool owner = existing == nullptr;
    const auto path = owner
                          ? std::filesystem::temp_directory_path() / ("dlog-manager-exit-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".log")
                          : std::filesystem::path(existing);
    if (owner)
    {
#ifdef _WIN32
        ASSERT_EQ(_putenv_s("DLOG_MANAGER_EXIT_TEST_FILE", path.string().c_str()), 0);
#else
        ASSERT_EQ(setenv("DLOG_MANAGER_EXIT_TEST_FILE", path.string().c_str(), 1), 0);
#endif
    }

    EXPECT_EXIT(
        {
            auto logger = AsyncLogger::BuildAsyncLogger<FileFlush>(path);
            LogManager::GetInstance().registerLogger("exit-file-probe", logger);
            logger->message("app.cpp", 1, LogLevel::INFO).stream() << "exit file probe";
            if (explicitStop) logger->stop();
            std::exit(EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");

    std::ifstream file(path, std::ios::binary);
    EXPECT_TRUE(file.is_open());
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("exit file probe"), std::string::npos);
    file.close();
    if (owner)
    {
        std::error_code error;
        std::filesystem::remove(path, error);
#ifdef _WIN32
        _putenv_s("DLOG_MANAGER_EXIT_TEST_FILE", "");
#else
        unsetenv("DLOG_MANAGER_EXIT_TEST_FILE");
#endif
    }
}


TEST(LogManagerExitFileDeathTest, ExplicitStopFlushesBeforeExit)
{
    verifyFileAtExit(true);
}

TEST(LogManagerExitFileDeathTest, NormalExitFlushesRegisteredLogger)
{
    verifyFileAtExit(false);
}

TEST(LogManagerDeathTest, TerminationReceivesFirstOutputException)
{
    EXPECT_EXIT(
        {
            std::set_terminate([]
                               {
                try
                {
                    if (auto error = std::current_exception()) std::rethrow_exception(error);
                }
                catch (const std::runtime_error &error)
                {
                    if (std::string(error.what()) == "output diagnostic") std::_Exit(42);
                }
                catch (...) {}
                std::_Exit(43); });
            auto logger = AsyncLogger::BuildAsyncLogger<DiagnosticFlush>();
            LogManager::GetInstance().registerLogger("diagnostic", logger);
            logger->message("app.cpp", 1, LogLevel::INFO).stream() << "record";
            std::exit(EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(42), "");
}


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
    std::set_terminate([]
    {
        const bool stopped = failures.load() == 2 && healthyText.find("healthy record\n") != std::string::npos;
        std::_Exit(stopped ? 42 : 43);
    });

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
        ::testing::ExitedWithCode(42), "");
}
