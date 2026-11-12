#include <gtest/gtest.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "log.h"

using namespace dlog;


namespace
{
    std::string_view basenameOf(std::string_view path)
    {
        auto separator = path.find_last_of("/\\");
        return std::string_view::npos == separator ? path : path.substr(separator + 1);
    }

    class RecordingFlush : public LogFlush
    {
    public:

        explicit RecordingFlush(std::string &text) : m_text(text) {}
        void writeAndFlush(const char *data, size_t size) override { m_text.append(data, size); }

    private:

        std::string &m_text;
    };
}


TEST(FileNameViewTest, ExposesAStringView)
{
    FileNameView file("Logger.cc");

    static_assert(std::is_same_v<decltype(file.view()), std::string_view>);
    static_assert(noexcept(file.view()));

    EXPECT_EQ(file.view(), "Logger.cc");
    EXPECT_EQ(file.view().size(), 9u);
}

TEST(FileNameViewTest, KeepsAPlainFileNameUnchanged)
{
    FileNameView file("Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, ExtractsFileNameFromUnixPath)
{
    FileNameView file("/home/user/project/logger/Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, ExtractsFileNameFromWindowsPath)
{
    FileNameView file(R"(D:\Workspace\dlog\src\core\logger.h)");

    EXPECT_EQ(file.view(), "logger.h");
}

TEST(FileNameViewTest, SupportsMixedPathSeparators)
{
    FileNameView file(R"(project\src/core\Logger.cc)");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, UsesTheLastPathSeparator)
{
    FileNameView file("/home/user/project/logger/Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
    EXPECT_NE(file.view(), "logger/Logger.cc");
}

TEST(FileNameViewTest, ReturnsAnEmptyViewForAnEmptyPath)
{
    FileNameView file("");

    EXPECT_TRUE(file.view().empty());
    EXPECT_EQ(file.view().size(), 0u);
}

TEST(FileNameViewTest, ReturnsAnEmptyViewWhenPathEndsWithUnixSeparator)
{
    FileNameView file("/home/user/project/logger/");

    EXPECT_TRUE(file.view().empty());
}

TEST(FileNameViewTest, ReturnsAnEmptyViewWhenPathEndsWithWindowsSeparator)
{
    FileNameView file(R"(D:\Workspace\dlog\src\core\)");

    EXPECT_TRUE(file.view().empty());
}

TEST(FileNameViewTest, HandlesRepeatedPathSeparators)
{
    FileNameView file("/home//user///Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, HandlesRootOnlyPaths)
{
    FileNameView unixRoot("/");
    FileNameView windowsRoot(R"(\)");

    EXPECT_TRUE(unixRoot.view().empty());
    EXPECT_TRUE(windowsRoot.view().empty());
}

TEST(FileNameViewTest, PreservesTheOriginalString)
{
    std::string path = "/home/user/project/Logger.cc";
    const std::string original = path;

    FileNameView file(path.c_str());

    EXPECT_EQ(path, original);
    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, DoesNotCopyTheFileNameData)
{
    std::string path = "/home/user/project/Logger.cc";
    FileNameView file(path.c_str());

    const auto separator = path.find_last_of("/\\");
    ASSERT_NE(separator, std::string::npos);

    EXPECT_EQ(file.view().data(), path.data() + separator + 1);
}

TEST(FileNameViewTest, WorksWithTheFileMacro)
{
    const std::string_view sourceFile = __FILE__;
    const std::string_view expected = basenameOf(sourceFile);

    FileNameView file(__FILE__);

    EXPECT_EQ(file.view(), expected);
}

TEST(FileNameViewTest, ViewCanOutliveATemporaryFileNameView)
{
    const std::string_view view = FileNameView("temporary/Logger.cc").view();

    // FileNameView 临时对象已经销毁，但字符串字面量具有静态存储期，仍然有效。
    EXPECT_EQ(view, "Logger.cc");
}


TEST(LogMacroTest, ExplicitLevelsCaptureLocationAndContinueAfterFatal)
{
    std::string text;
    auto logger = LogManager::GetInstance().buildLogger<RecordingFlush>("logger-test-174", text);
    const int line = __LINE__ + 1;
    DLOG_LOGGER_TRACE(logger) << "trace";
    DLOG_LOGGER_DEBUG(logger) << "debug";
    DLOG_LOGGER_INFO(logger) << "info";
    DLOG_LOGGER_WARN(logger) << "warn";
    DLOG_LOGGER_ERROR(logger) << "error";
    DLOG_LOGGER_FATAL(logger) << "fatal";
    DLOG_LOGGER_INFO(logger) << "after fatal";
    logger->stop();
    EXPECT_NE(text.find("[logger_test.cpp:" + std::to_string(line) + "] trace\n"), std::string::npos);
    for (const char *level : {"[TRACE]", "[DEBUG]", "[INFO ]", "[WARN ]", "[ERROR]", "[FATAL]"})
        EXPECT_NE(text.find(level), std::string::npos);
    EXPECT_NE(text.find("after fatal"), std::string::npos);
    EXPECT_EQ(text.find("after fatal"), text.rfind("after fatal"));
}

TEST(LogMacroTest, ExplicitLoggersUseSeparateTargets)
{
    std::string firstText, secondText;
    auto first = LogManager::GetInstance().buildLogger<RecordingFlush>("logger-test-194", firstText);
    auto second = LogManager::GetInstance().buildLogger<RecordingFlush>("logger-test-195", secondText);
    DLOG_LOGGER_INFO(first) << "first only";
    DLOG_LOGGER_INFO(second) << "second only";
    first->stop();
    second->stop();
    EXPECT_NE(firstText.find("first only"), std::string::npos);
    EXPECT_EQ(firstText.find("second only"), std::string::npos);
    EXPECT_NE(secondText.find("second only"), std::string::npos);
    EXPECT_EQ(secondText.find("first only"), std::string::npos);
}

TEST(LogMacroTest, TargetAndSavedErrnoAreEvaluatedOnceAndNullRejected)
{
    std::string text;
    auto logger = LogManager::GetInstance().buildLogger<RecordingFlush>("logger-test-209", text);
    int targets = 0;
    int errors = 0;
    DLOG_LOGGER_SYS_ERROR((++targets, logger), (++errors, 1234567)) << "system error";
    DLOG_LOGGER_SYS_FATAL(logger, 1234568) << "fatal system error";
    EXPECT_THROW(DLOG_LOGGER_INFO(Logger::SharedPtr{}) << "invalid", std::invalid_argument);
    logger->stop();
    EXPECT_EQ(targets, 1);
    EXPECT_EQ(errors, 1);
    EXPECT_NE(text.find("(errno=1234567) system error"), std::string::npos);
    EXPECT_NE(text.find("(errno=1234568) fatal system error"), std::string::npos);
}

TEST(LogMacroDeathTest, DefaultMacrosWriteToDefaultConsole)
{
    EXPECT_EXIT(
        {
            ::testing::internal::CaptureStdout();
            DLOG_TRACE() << "default trace";
            DLOG_DEBUG() << "default debug";
            DLOG_INFO() << "default console";
            DLOG_WARN() << "default warn";
            DLOG_ERROR() << "default error";
            DLOG_FATAL() << "default fatal level";
            DLOG_SYS_ERROR(1234568) << "default system error";
            DLOG_SYS_FATAL(1234567) << "default fatal";
            auto logger = LogManager::GetInstance().getDefaultLogger();
            logger->stop();
            auto text = ::testing::internal::GetCapturedStdout();
            std::_Exit(text.find("default trace") != std::string::npos &&
                               text.find("default debug") != std::string::npos &&
                               text.find("default console") != std::string::npos &&
                               text.find("default warn") != std::string::npos &&
                               text.find("default error") != std::string::npos &&
                               text.find("default fatal level") != std::string::npos &&
                               text.find("(errno=1234568) default system error") != std::string::npos &&
                               text.find("(errno=1234567) default fatal") != std::string::npos
                           ? 0
                           : 1);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(LogMacroDeathTest, DefaultLoggerDrainsOnNormalExit)
{
    EXPECT_EXIT(
        {
            ::testing::internal::CaptureStdout();
            std::atexit([]
                        {
            auto text = ::testing::internal::GetCapturedStdout();
            std::_Exit(text.find("default exit tail") != std::string::npos ? 0 : 1); });
            DLOG_INFO() << "default exit tail";
            std::exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}


namespace
{

    std::vector<std::filesystem::path> testDirectories;

    class LoggerTestCleanup : public ::testing::Environment
    {
        void SetUp() override
        {
            // Manager 在测试中创建，其析构先于此回调，确保文件句柄已关闭。
            ASSERT_EQ(std::atexit([]
                                  {
                for (const auto &directory : testDirectories)
                {
                    std::error_code error;
                    std::filesystem::remove_all(directory, error);
                } }),
                      0);
        }

        void TearDown() override { EXPECT_NO_THROW(LogManager::Shutdown()); }
    };

    [[maybe_unused]] const auto *cleanup = ::testing::AddGlobalTestEnvironment(new LoggerTestCleanup);

    class LoggerFileTest : public ::testing::Test
    {
    protected:

        void SetUp() override
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            m_directory = std::filesystem::temp_directory_path() / ("dlog_logger_test_" + std::to_string(suffix));
            std::filesystem::create_directories(m_directory);
            testDirectories.push_back(m_directory);
        }

        std::filesystem::path m_directory;

        std::string readFile(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            EXPECT_TRUE(input);
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }
    };

    class TrackedFlush : public LogFlush
    {
    public:

        TrackedFlush(int &constructions, int &destructions, std::unique_ptr<int> value)
            : m_destructions(destructions), m_value(std::move(value)) { ++constructions; }
        ~TrackedFlush() override { ++m_destructions; }
        void writeAndFlush(const char *, size_t) override {}

    private:

        int &m_destructions;
        std::unique_ptr<int> m_value;
    };

} // namespace


TEST_F(LoggerFileTest, CreatesBothFileModesWithNativePaths)
{
    const auto filename = m_directory / "normal" / std::filesystem::u8path(u8"\u65e5\u5fd7 \U0001f680.log");
    const auto basePath = m_directory / "rolling" / std::filesystem::u8path(u8"\u8f6e\u8f6c \U0001f680");
    auto normalLogger = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-321", filename);
    auto rollingLogger = LogManager::GetInstance().buildLogger<RollFileFlush>("logger-test-322", basePath, 1024);
    ASSERT_NE(normalLogger, nullptr);
    ASSERT_NE(rollingLogger, nullptr);
    rollingLogger->message("app.cpp", 42, LogLevel::INFO).stream() << "rolling record";
    EXPECT_NO_THROW(normalLogger->stop());
    EXPECT_NO_THROW(rollingLogger->stop());

    ASSERT_TRUE(std::filesystem::is_regular_file(filename));
    EXPECT_EQ(std::filesystem::file_size(filename), 0u);
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(basePath.parent_path())) paths.push_back(entry.path());
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths.front().filename().u8string().find(basePath.filename().u8string() + '.'), 0u);
    EXPECT_EQ(paths.front().extension(), ".log");
    const auto rollingText = readFile(paths.front());
    EXPECT_NE(rollingText.find("[app.cpp:42] rolling record\n"), std::string::npos);
    EXPECT_EQ(rollingText.find('\x1b'), std::string::npos);
}

TEST_F(LoggerFileTest, ConstructionErrorsPreserveExistingFileAndAllowRetry)
{
    const auto blocked = m_directory / "blocked";
    {
        std::ofstream output(blocked, std::ios::binary);
        ASSERT_TRUE(output);
        output << "keep";
    }
    const auto invalidPath = blocked / "nested" / "app";
    const auto recovered = m_directory / "recovered.log";
    const int workersBefore = Thread::NumCreated();

    EXPECT_THROW(LogManager::GetInstance().buildLogger<FileFlush>("logger-test-353", std::filesystem::path{}), std::invalid_argument);
    EXPECT_THROW(LogManager::GetInstance().buildLogger<RollFileFlush>("logger-test-354", std::filesystem::path{}, 1024), std::invalid_argument);
    EXPECT_THROW(LogManager::GetInstance().buildLogger<FileFlush>("logger-test-355", invalidPath), std::filesystem::filesystem_error);
    EXPECT_THROW(LogManager::GetInstance().buildLogger<RollFileFlush>("logger-test-356", invalidPath, 1024), std::filesystem::filesystem_error);
    EXPECT_EQ(Thread::NumCreated(), workersBefore);
    EXPECT_NO_THROW(LogManager::GetInstance().buildLogger<FileFlush>("logger-test-358", recovered));
    EXPECT_TRUE(std::filesystem::is_regular_file(recovered));

    std::ifstream input(blocked, std::ios::binary);
    ASSERT_TRUE(input);
    const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(contents, "keep");
}

TEST(LoggerLifetimeTest, ManagerRetainsLoggerAfterExternalOwnersRelease)
{
    static int constructions = 0;
    static int destructions = 0;
    auto value = std::make_unique<int>(42);
    auto logger = LogManager::GetInstance().buildLogger<TrackedFlush>("logger-test-372", constructions, destructions, std::move(value));
    EXPECT_EQ(value, nullptr);
    EXPECT_EQ(constructions, 1);
    EXPECT_EQ(destructions, 0);
    auto anotherOwner = logger;
    std::weak_ptr<Logger> observer = logger;
    logger.reset();
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(destructions, 0);

    anotherOwner.reset();
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(destructions, 0);
    observer.lock()->stop();
}

TEST_F(LoggerFileTest, ExplicitStopDrainsOnlyItsOwnTarget)
{
    const auto firstPath = m_directory / "first.log";
    const auto secondPath = m_directory / "second.log";
    auto first = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-392", firstPath);
    auto second = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-393", secondPath);

    first->message("src/app.cpp", 42, LogLevel::INFO).stream() << "first record";
    second->message("src/app.cpp", 43, LogLevel::WARN).stream() << "second record";
    first->stop();
    second->stop();

    const auto firstText = readFile(firstPath);
    const auto secondText = readFile(secondPath);
    EXPECT_NE(firstText.find("[app.cpp:42] first record\n"), std::string::npos);
    EXPECT_NE(secondText.find("[app.cpp:43] second record\n"), std::string::npos);
    EXPECT_EQ(firstText.find("second record"), std::string::npos);
    EXPECT_EQ(secondText.find("first record"), std::string::npos);
    EXPECT_EQ(firstText.find('\x1b'), std::string::npos);
    EXPECT_EQ(secondText.find('\x1b'), std::string::npos);
}


TEST_F(LoggerFileTest, LiveMessageRemainsValidAfterExternalOwnerReleases)
{
    const auto path = m_directory / "auto.log";
    auto logger = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-414", path);
    std::weak_ptr<Logger> observer = logger;
    {
        auto message = logger->message("app.cpp", 42, LogLevel::INFO);
        logger.reset();
        EXPECT_FALSE(observer.expired());
        message.stream() << "last record";
    }
    EXPECT_FALSE(observer.expired());
    observer.lock()->stop();
    EXPECT_NE(readFile(path).find("[app.cpp:42] last record\n"), std::string::npos);
}


TEST_F(LoggerFileTest, ConcurrentRecordsStayWholeAcrossBufferBoundaries)
{
    const auto path = m_directory / "concurrent.log";
    auto logger = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-430", path);
    std::string longRecord(2 * kLargeFixedBufferSize + 1, 'a');
    longRecord[kLargeFixedBufferSize] = '\0';
    const std::string otherRecord(kLargeFixedBufferSize + 1, 'b');

    std::thread first([&]
                      { logger->message("app.cpp", 41, LogLevel::INFO).stream() << longRecord; });
    std::thread second([&]
                       { logger->message("app.cpp", 42, LogLevel::INFO).stream() << otherRecord; });
    first.join();
    second.join();
    logger->stop();

    std::ifstream input(path, std::ios::binary);
    ASSERT_TRUE(input);
    std::string line;
    int firstRecords = 0;
    for (int i = 0; i < 2; ++i)
    {
        ASSERT_TRUE(std::getline(input, line));
        const auto firstSource = line.find("[app.cpp:41] ");
        const bool isFirst = firstSource != std::string::npos;
        const auto source = isFirst ? firstSource : line.find("[app.cpp:42] ");
        ASSERT_NE(source, std::string::npos);
        if (isFirst) ++firstRecords;
        const auto &expected = isFirst ? longRecord : otherRecord;
        EXPECT_EQ(line.compare(source + sizeof("[app.cpp:41] ") - 1, std::string::npos, expected), 0);
    }
    EXPECT_EQ(firstRecords, 1);
    EXPECT_FALSE(std::getline(input, line));
}


TEST_F(LoggerFileTest, RejectsUnsupportedLevelsAndMessagesAfterStop)
{
    const auto path = m_directory / "stopped.log";
    auto logger = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-466", path);

    EXPECT_THROW(logger->message("app.cpp", 42, LogLevel::LEVEL_COUNT), std::invalid_argument);
    testing::internal::CaptureStderr();
    {
        auto message = logger->message("app.cpp", 42, LogLevel::INFO);
        message.stream() << "too late";
        EXPECT_NO_THROW(logger->stop());
    }
    EXPECT_NE(testing::internal::GetCapturedStderr().find("log message submission failed"), std::string::npos);
    EXPECT_THROW(logger->message("app.cpp", 43, LogLevel::INFO), std::logic_error);
    EXPECT_TRUE(readFile(path).empty());
}


TEST(LoggerFailureTest, StopReportsBackgroundErrorAfterJoining)
{
    EXPECT_EXIT(
        {
            class FailingFlush : public LogFlush
            {
            public:

                explicit FailingFlush(int &calls) : m_calls(calls) {}
                void writeAndFlush(const char *, size_t) override
                {
                    ++m_calls;
                    throw std::runtime_error("output failed");
                }

            private:

                int &m_calls;
            };

            int calls = 0;
            auto logger = LogManager::GetInstance().buildLogger<FailingFlush>("logger-test-500", calls);
            logger->message("app.cpp", 1, LogLevel::INFO).stream() << "record";
            for (int i = 0; i < 2; ++i)
            {
                try
                {
                    logger->stop();
                    FAIL() << "expected output failure";
                }
                catch (const std::runtime_error &error)
                {
                    EXPECT_STREQ(error.what(), "output failed");
                }
            }
            EXPECT_EQ(calls, 1);
            EXPECT_THROW(logger->message("app.cpp", 2, LogLevel::INFO), std::logic_error);

            std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}


TEST(LoggerMessageTest, DefaultStdoutColorsTheLevel)
{
    auto logger = LogManager::GetInstance().buildLogger("logger-test-521");
    testing::internal::CaptureStdout();
    logger->message("app.cpp", 42, LogLevel::INFO).stream() << "console record";
    EXPECT_NO_THROW(logger->stop());
    const auto text = testing::internal::GetCapturedStdout();
    EXPECT_NO_THROW(logger->stop());
    EXPECT_NE(text.find("\x1b["), std::string::npos);
    EXPECT_NE(text.find("[app.cpp:42] console record"), std::string::npos);
}


TEST(LoggerMessageTest, CustomOutputCanEnableLevelColor)
{
    class ColoredFlush : public LogFlush
    {
    public:

        explicit ColoredFlush(std::string &text) : LogFlush(LogLevelColorMode::ON), m_text(text) {}
        void writeAndFlush(const char *data, size_t len) override { m_text.append(data, len); }

    private:

        std::string &m_text;
    };

    std::string text;
    auto logger = LogManager::GetInstance().buildLogger<ColoredFlush>("logger-test-547", text);
    logger->message("app.cpp", 42, LogLevel::INFO).stream() << "custom record";
    logger->stop();

    EXPECT_NE(text.find("\x1b["), std::string::npos);
    EXPECT_NE(text.find("[app.cpp:42] custom record\n"), std::string::npos);
}


TEST_F(LoggerFileTest, MessagePreservesSavedErrno)
{
    const auto path = m_directory / "errno.log";
    Logger::SharedPtr logger = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-559", path);
    logger->message("app.cpp", 42, LogLevel::ERROR, EACCES).stream() << "open failed";
    logger->stop();

    const auto errorText = std::error_code(EACCES, std::generic_category()).message();
    const auto expected = "[ERROR] [app.cpp:42] " + errorText + " (errno=" + std::to_string(EACCES) + ") open failed\n";
    EXPECT_NE(readFile(path).find(expected), std::string::npos);
}

TEST_F(LoggerFileTest, FatalAndFollowingRecordsAreWrittenOnStop)
{
    const auto path = m_directory / "fatal.log";
    auto logger = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-571", path);
    logger->message("app.cpp", 1, LogLevel::FATAL, 1234567).stream() << "fatal record";
    logger->message("app.cpp", 2, LogLevel::INFO).stream() << "after fatal";
    logger->stop();
    const auto text = readFile(path);
    EXPECT_NE(text.find("[FATAL]"), std::string::npos);
    EXPECT_NE(text.find("(errno=1234567) fatal record"), std::string::npos);
    EXPECT_NE(text.find("after fatal"), std::string::npos);
}

TEST_F(LoggerFileTest, RollingLoggerReopensUnicodePathWithoutLosingRecords)
{
    const auto base = m_directory / std::filesystem::u8path(u8"日志 路径") / "app";
    {
        auto logger = LogManager::GetInstance().buildLogger<RollFileFlush>("logger-test-585", base, 1024);
        logger->message("app.cpp", 1, LogLevel::INFO).stream() << "first run";
        logger->stop();
    }
    {
        auto logger = LogManager::GetInstance().buildLogger<RollFileFlush>("logger-test-590", base, 1024);
        logger->message("app.cpp", 2, LogLevel::INFO).stream() << "second run";
        logger->stop();
    }
    std::string text;
    for (const auto &entry : std::filesystem::directory_iterator(base.parent_path()))
        text += readFile(entry.path());
    EXPECT_NE(text.find("first run"), std::string::npos);
    EXPECT_NE(text.find("second run"), std::string::npos);
}

#if defined(DLOG_OS_LINUX)
TEST(LoggerFailureTest, FullDeviceErrorIsReportedOnStop)
{
    if (!std::filesystem::exists("/dev/full")) GTEST_SKIP() << "/dev/full is unavailable";
    EXPECT_EXIT(
        {
            auto logger = LogManager::GetInstance().buildLogger<FileFlush>("logger-test-605", "/dev/full");
            logger->message("app.cpp", 1, LogLevel::ERROR).stream() << "write failure";
            EXPECT_THROW(logger->stop(), std::exception);

            std::_Exit(::testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS);
        },
        ::testing::ExitedWithCode(EXIT_SUCCESS), "");
}
#endif
