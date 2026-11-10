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
#include <thread>
#include <vector>


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

    class DiscardFlush : public LogFlush
    {
    public:

        void writeAndFlush(const char *, size_t) override {}
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
        std::_Exit(stopped ? 42 : 43); });

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

TEST(LogManagerDeathTest, ConcurrentFirstAccessReturnsTheSameManagerAndDefaultLogger)
{
    EXPECT_EXIT(
        {
            constexpr size_t count = 8;
            std::vector<LogManager *> managers(count);
            std::vector<AsyncLogger::Ptr> defaults(count);
            std::atomic_bool start{false};
            std::vector<std::thread> threads;
            for (size_t i = 0; i < count; ++i)
                threads.emplace_back([&, i]
                                     {
                    while (!start.load()) std::this_thread::yield();
                    managers[i] = &LogManager::GetInstance();
                    defaults[i] = managers[i]->getDefaultLogger(); });
            start = true;
            for (auto &thread : threads) thread.join();
            for (size_t i = 0; i < count; ++i)
            {
                EXPECT_EQ(managers[i], managers[0]);
                EXPECT_EQ(defaults[i], defaults[0]);
                EXPECT_NE(defaults[i], nullptr);
            }
            std::exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(LogManagerTest, MissingLookupDoesNotReserveAName)
{
    auto &manager = LogManager::GetInstance();
    EXPECT_EQ(manager.getLogger(""), nullptr);
    EXPECT_EQ(manager.getLogger("lookup-then-register"), nullptr);
    auto logger = AsyncLogger::BuildAsyncLogger<DiscardFlush>();
    manager.registerLogger("lookup-then-register", logger);
    EXPECT_EQ(manager.getLogger("lookup-then-register"), logger);
}

TEST(LogManagerTest, InvalidRegistrationLeavesTheNameAndDefaultUnchanged)
{
    auto &manager = LogManager::GetInstance();
    auto defaultLogger = manager.getDefaultLogger();
    auto logger = AsyncLogger::BuildAsyncLogger<DiscardFlush>();
    EXPECT_THROW(manager.registerLogger("", logger), std::invalid_argument);
    EXPECT_THROW(manager.registerLogger("invalid-then-valid", nullptr), std::invalid_argument);
    EXPECT_EQ(manager.getLogger(""), nullptr);
    EXPECT_EQ(manager.getLogger("invalid-then-valid"), nullptr);
    EXPECT_NO_THROW(manager.registerLogger("invalid-then-valid", logger));
    EXPECT_EQ(manager.getLogger("invalid-then-valid"), logger);
    EXPECT_EQ(manager.getDefaultLogger(), defaultLogger);
}

TEST(LogManagerTest, DuplicateNamePreservesOriginalAndDoesNotConsumeReplacement)
{
    auto &manager = LogManager::GetInstance();
    auto original = AsyncLogger::BuildAsyncLogger<DiscardFlush>();
    auto replacement = AsyncLogger::BuildAsyncLogger<DiscardFlush>();
    manager.registerLogger("duplicate-original", original);
    EXPECT_THROW(manager.registerLogger("duplicate-original", replacement), std::logic_error);
    EXPECT_EQ(manager.getLogger("duplicate-original"), original);
    EXPECT_NO_THROW(replacement->message("app.cpp", 1, LogLevel::INFO).stream() << "still usable");
    EXPECT_NO_THROW(manager.registerLogger("duplicate-replacement", replacement));
    EXPECT_EQ(manager.getLogger("duplicate-replacement"), replacement);
}

TEST(LogManagerTest, NamedDefaultDoesNotReplaceTheDefaultLogger)
{
    auto &manager = LogManager::GetInstance();
    auto defaultLogger = manager.getDefaultLogger();
    auto named = AsyncLogger::BuildAsyncLogger<DiscardFlush>();
    manager.registerLogger("default", named);
    EXPECT_EQ(manager.getLogger("default"), named);
    EXPECT_EQ(manager.getDefaultLogger(), defaultLogger);
    EXPECT_NE(manager.getDefaultLogger(), named);
}

TEST(LogManagerTest, RetainsRegisteredLoggerAfterCallerReleasesOwnership)
{
    auto &manager = LogManager::GetInstance();
    std::weak_ptr<AsyncLogger> observer;
    {
        auto logger = AsyncLogger::BuildAsyncLogger<DiscardFlush>();
        observer = logger;
        manager.registerLogger("retained", logger);
    }
    ASSERT_FALSE(observer.expired());
    auto retained = manager.getLogger("retained");
    ASSERT_NE(retained, nullptr);
    EXPECT_EQ(retained, observer.lock());
    EXPECT_NO_THROW(retained->message("app.cpp", 2, LogLevel::INFO).stream() << "retained");
    EXPECT_NO_THROW(retained->stop());
}

TEST(LogManagerTest, ConcurrentRegistrationAndLookupKeepEachTarget)
{
    auto &manager = LogManager::GetInstance();
    constexpr size_t count = 8;
    std::vector<AsyncLogger::Ptr> loggers;
    for (size_t i = 0; i < count; ++i) loggers.push_back(AsyncLogger::BuildAsyncLogger<DiscardFlush>());
    std::atomic_bool start{false};
    std::atomic_int errors{0};
    std::vector<std::thread> threads;
    for (size_t i = 0; i < count; ++i)
        threads.emplace_back([&, i]
                             {
            while (!start.load()) std::this_thread::yield();
            try
            {
                const auto name = "concurrent-" + std::to_string(i);
                manager.registerLogger(name, loggers[i]);
                for (size_t j = 0; j < count; ++j)
                {
                    if (manager.getLogger(name) != loggers[i]) ++errors;
                    auto found = manager.getLogger("concurrent-" + std::to_string(j));
                    if (found && found != loggers[j]) ++errors;
                    if (!manager.getDefaultLogger()) ++errors;
                }
            }
            catch (...) { ++errors; } });
    start = true;
    for (auto &thread : threads) thread.join();
    EXPECT_EQ(errors.load(), 0);
    for (size_t i = 0; i < count; ++i)
        EXPECT_EQ(manager.getLogger("concurrent-" + std::to_string(i)), loggers[i]);
}

TEST(LogManagerTest, ConcurrentDuplicateRegistrationHasExactlyOneWinner)
{
    auto &manager = LogManager::GetInstance();
    constexpr size_t count = 8;
    std::vector<AsyncLogger::Ptr> loggers;
    for (size_t i = 0; i < count; ++i) loggers.push_back(AsyncLogger::BuildAsyncLogger<DiscardFlush>());
    std::atomic_bool start{false};
    std::atomic_int successes{0}, duplicates{0}, errors{0};
    std::vector<std::thread> threads;
    for (size_t i = 0; i < count; ++i)
        threads.emplace_back([&, i]
                             {
            while (!start.load()) std::this_thread::yield();
            try
            {
                manager.registerLogger("concurrent-duplicate", loggers[i]);
                ++successes;
                if (manager.getLogger("concurrent-duplicate") != loggers[i]) ++errors;
            }
            catch (const std::logic_error &) { ++duplicates; }
            catch (...) { ++errors; } });
    start = true;
    for (auto &thread : threads) thread.join();
    EXPECT_EQ(successes.load(), 1);
    EXPECT_EQ(duplicates.load(), static_cast<int>(count - 1));
    EXPECT_EQ(errors.load(), 0);
    auto winner = manager.getLogger("concurrent-duplicate");
    bool known = false;
    for (const auto &logger : loggers) known = known || winner == logger;
    EXPECT_TRUE(known);
}
