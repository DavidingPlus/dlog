#include <gtest/gtest.h>

#include "logmanager.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace dlog;

extern LogManager &GetInstanceFromAnotherSourceFile();
extern void ConfigureThreadPoolFromAnotherSourceFile(size_t threadCount);


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
            auto logger = Logger::BuildLogger<FileFlush>(path);
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
            auto logger = Logger::BuildLogger<DiagnosticFlush>();
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
    EXPECT_EQ(&manager, &GetInstanceFromAnotherSourceFile());
    EXPECT_EQ(manager.getLogger("missing"), nullptr);

    auto console = manager.getDefaultLogger();
    EXPECT_NE(console, nullptr);
    auto first = Logger::BuildLogger<RecordingFlush>(firstText);
    auto second = Logger::BuildLogger<RecordingFlush>(secondText);
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
    auto first = Logger::BuildLogger<FailingFlush>(failures);
    auto second = Logger::BuildLogger<FailingFlush>(failures);
    auto healthy = Logger::BuildLogger<RecordingFlush>(healthyText);
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
            std::vector<Logger::Ptr> defaults(count);
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

static void verifyConfiguredThreadCount()
{
    const int before = Thread::NumCreated();
    EXPECT_THROW(LogManager::ConfigureThreadPool(0), std::invalid_argument);
    LogManager::ConfigureThreadPool(2);
    ConfigureThreadPoolFromAnotherSourceFile(3);
    EXPECT_EQ(Thread::NumCreated(), before);
    auto &manager = LogManager::GetInstance();
    // 除共享池的 3 个 Thread 外，默认 Logger 还创建了一个 Thread。
    EXPECT_EQ(Thread::NumCreated() - before, 4);
    EXPECT_EQ(&manager, &GetInstanceFromAnotherSourceFile());
    ::testing::internal::CaptureStderr();
    LogManager::ConfigureThreadPool(5);
    LogManager::ConfigureThreadPool(0);
    const auto warning = ::testing::internal::GetCapturedStderr();
    EXPECT_NE(warning.find("configuration ignored"), std::string::npos);
    EXPECT_EQ(Thread::NumCreated() - before, 4);
    auto result = manager.getThreadPool()->enqueue([]
                                                   { return 42; });
    EXPECT_EQ(result.get(), 42);
}

TEST(LogManagerDeathTest, ConfiguredThreadCountIsUsedAndLateConfigurationIsIgnored)
{
    EXPECT_EXIT(
        {
            verifyConfiguredThreadCount();
            std::exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(LogManagerDeathTest, DefaultThreadCountIsFour)
{
    EXPECT_EXIT(
        {
            const int before = Thread::NumCreated();
            LogManager::GetInstance();
            EXPECT_EQ(Thread::NumCreated() - before, 5);
            std::exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(LogManagerDeathTest, ConcurrentConfigurationAndFirstAccessAreConsistent)
{
    EXPECT_EXIT(
        {
            const int before = Thread::NumCreated();
            std::atomic_bool start{false};
            ::testing::internal::CaptureStderr();
            std::thread configure([&]
                                  {
                while (!start.load()) std::this_thread::yield();
                LogManager::ConfigureThreadPool(3); });
            std::thread initialize([&]
                                   {
                while (!start.load()) std::this_thread::yield();
                LogManager::GetInstance(); });
            start = true;
            configure.join();
            initialize.join();
            const auto warning = ::testing::internal::GetCapturedStderr();
            const bool ignored = warning.find("configuration ignored") != std::string::npos;
            EXPECT_EQ(Thread::NumCreated() - before, ignored ? 5 : 4);
            std::exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(LogManagerTest, MissingLookupDoesNotReserveAName)
{
    auto &manager = LogManager::GetInstance();
    EXPECT_EQ(manager.getLogger(""), nullptr);
    EXPECT_EQ(manager.getLogger("lookup-then-register"), nullptr);
    auto logger = Logger::BuildLogger<DiscardFlush>();
    manager.registerLogger("lookup-then-register", logger);
    EXPECT_EQ(manager.getLogger("lookup-then-register"), logger);
}

TEST(LogManagerTest, SharesThreadPoolAndExecutesTasks)
{
    auto &manager = LogManager::GetInstance();
    auto first = manager.getThreadPool();
    auto second = LogManager::GetInstance().getThreadPool();

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, GetInstanceFromAnotherSourceFile().getThreadPool());
    auto result = first->enqueue([]
                                 { return 42; });
    ASSERT_EQ(result.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(result.get(), 42);
}

TEST(LogManagerDeathTest, DrainsThreadPoolTasksAtExit)
{
    EXPECT_EXIT(
        {
            static std::atomic_int completed{0};
            static std::atomic_bool release{false};
            std::atexit([]
                        { std::_Exit(completed.load() == 20 ? EXIT_SUCCESS : EXIT_FAILURE); });
            auto &manager = LogManager::GetInstance();
            for (int i = 0; i < 20; ++i)
                manager.getThreadPool()->enqueue([]
                                                 {
                    while (!release.load()) std::this_thread::yield();
                    ++completed; });
            // 后注册的回调先执行，让 Manager 析构前已有任务处于等待或排队状态。
            std::atexit([]
                        { release = true; });
            std::exit(EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

static void verifyExplicitShutdown()
{
    std::string text;
    auto &manager = LogManager::GetInstance();
    auto logger = Logger::BuildLogger<RecordingFlush>(text);
    manager.registerLogger("shutdown", logger);
    auto pool = manager.getThreadPool();
    std::promise<void> started, release;
    auto releaseSignal = release.get_future().share();
    auto task = pool->enqueue([logger, &started, releaseSignal]
                              {
        started.set_value();
        releaseSignal.wait();
        logger->message("app.cpp", 1, LogLevel::INFO).stream() << "task record"; });
    started.get_future().wait();
    auto shutdown = std::async(std::launch::async, []
                               { LogManager::Shutdown(); });
    EXPECT_EQ(shutdown.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    release.set_value();
    shutdown.get();
    task.get();
    EXPECT_NE(text.find("task record"), std::string::npos);
    EXPECT_THROW(pool->enqueue([] {}), std::runtime_error);
    EXPECT_THROW(logger->message("app.cpp", 1, LogLevel::INFO), std::logic_error);
    EXPECT_THROW(manager.registerLogger("after-shutdown", logger), std::logic_error);
    EXPECT_NO_THROW(LogManager::Shutdown());
}

static void verifyExplicitShutdownAfterFailure()
{
    std::atomic_int failures{0};
    std::string text;
    auto &manager = LogManager::GetInstance();
    auto failing = Logger::BuildLogger<FailingFlush>(failures);
    auto healthy = Logger::BuildLogger<RecordingFlush>(text);
    manager.registerLogger("failing", failing);
    manager.registerLogger("healthy", healthy);
    failing->message("app.cpp", 1, LogLevel::INFO).stream() << "failure";
    healthy->message("app.cpp", 2, LogLevel::INFO).stream() << "healthy record";
    EXPECT_THROW(LogManager::Shutdown(), OutputError);
    EXPECT_EQ(failures.load(), 1);
    EXPECT_NE(text.find("healthy record"), std::string::npos);
    EXPECT_THROW(manager.getThreadPool()->enqueue([] {}), std::runtime_error);
    EXPECT_NO_THROW(LogManager::Shutdown());
}

TEST(LogManagerDeathTest, ExplicitShutdownDrainsTasksAndLoggers)
{
    EXPECT_EXIT(
        {
            verifyExplicitShutdown();
            std::exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(LogManagerDeathTest, ExplicitShutdownStopsAllAfterOutputFailure)
{
    EXPECT_EXIT(
        {
            verifyExplicitShutdownAfterFailure();
            std::exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}

TEST(LogManagerTest, InvalidRegistrationLeavesTheNameAndDefaultUnchanged)
{
    auto &manager = LogManager::GetInstance();
    auto defaultLogger = manager.getDefaultLogger();
    auto logger = Logger::BuildLogger<DiscardFlush>();
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
    auto original = Logger::BuildLogger<DiscardFlush>();
    auto replacement = Logger::BuildLogger<DiscardFlush>();
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
    auto named = Logger::BuildLogger<DiscardFlush>();
    manager.registerLogger("default", named);
    EXPECT_EQ(manager.getLogger("default"), named);
    EXPECT_EQ(manager.getDefaultLogger(), defaultLogger);
    EXPECT_NE(manager.getDefaultLogger(), named);
}

TEST(LogManagerTest, RetainsRegisteredLoggerAfterCallerReleasesOwnership)
{
    auto &manager = LogManager::GetInstance();
    std::weak_ptr<Logger> observer;
    {
        auto logger = Logger::BuildLogger<DiscardFlush>();
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
    std::vector<Logger::Ptr> loggers;
    for (size_t i = 0; i < count; ++i) loggers.push_back(Logger::BuildLogger<DiscardFlush>());
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
    std::vector<Logger::Ptr> loggers;
    for (size_t i = 0; i < count; ++i) loggers.push_back(Logger::BuildLogger<DiscardFlush>());
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

#ifdef _WIN32


namespace
{
    void checkDllExit()
    {
        wchar_t executable[32768];
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        ASSERT_GT(length, 0u);
        ASSERT_LT(length, std::size(executable));

        const auto prefix = std::filesystem::temp_directory_path() /
                            ("dlog-dll-exit-" + std::to_string(GetCurrentProcessId()) + "-" +
                             std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        const auto outputPath = prefix.wstring() + L".output";
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE output = CreateFileW(outputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(output, INVALID_HANDLE_VALUE);

        // 使用当前测试程序的子进程，使 DLL 退出卡住时可由父进程超时结束。
        std::wstring command = L"\"" + std::wstring(executable) +
                               L"\" --gtest_filter=LogManagerDllExitTest.ExplicitShutdown --dlog-dll-exit=" + prefix.wstring();
        std::vector<wchar_t> buffer(command.begin(), command.end());
        buffer.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        const bool started = CreateProcessW(executable, buffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                            nullptr, std::filesystem::path(executable).parent_path().c_str(),
                                            &startup, &process) != FALSE;
        CloseHandle(output);
        ASSERT_TRUE(started);

        const DWORD wait = WaitForSingleObject(process.hProcess, 10000);
        if (wait == WAIT_TIMEOUT)
        {
            TerminateProcess(process.hProcess, 124);
            WaitForSingleObject(process.hProcess, 5000);
        }
        DWORD exitCode = static_cast<DWORD>(-1);
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        const auto logPath = prefix.string() + ".log";
        const auto taskPath = prefix.string() + ".task";
        auto readFile = [](const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        };
        const auto log = readFile(logPath);
        const auto task = readFile(taskPath);
        const auto diagnostic = readFile(outputPath);
        std::error_code error;
        std::filesystem::remove(logPath, error);
        std::filesystem::remove(taskPath, error);
        std::filesystem::remove(outputPath, error);

        ASSERT_EQ(wait, WAIT_OBJECT_0) << diagnostic;
        ASSERT_EQ(exitCode, 0u) << diagnostic;
        EXPECT_NE(log.find("dll log at exit"), std::string::npos) << diagnostic;
        EXPECT_NE(task.find("dll task at exit"), std::string::npos) << diagnostic;
    }
}

TEST(LogManagerDllExitTest, ExplicitShutdown)
{
    const wchar_t *argument = std::wcsstr(GetCommandLineW(), L"--dlog-dll-exit=");
    if (!argument)
    {
        checkDllExit();
        return;
    }

    argument += std::wcslen(L"--dlog-dll-exit=");

    // 子进程内实际加载 DLL，父进程检查其退出状态和输出。
    wchar_t executable[32768];
    ASSERT_GT(GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable))), 0u);
    const auto dll = std::filesystem::path(executable).parent_path() / L"logmanager_dll_probe.dll";
    HMODULE module = LoadLibraryW(dll.c_str());
    ASSERT_NE(module, nullptr);
    const auto submit = reinterpret_cast<void (*)(const char *)>(GetProcAddress(module, "ProbeSubmit"));
    const auto flush = reinterpret_cast<void (*)()>(GetProcAddress(module, "ProbeFlush"));
    ASSERT_NE(submit, nullptr);
    ASSERT_NE(flush, nullptr);
    const auto prefix = std::filesystem::path(argument).string();
    submit(prefix.c_str());
    flush();
    ASSERT_TRUE(FreeLibrary(module));
}


#endif
