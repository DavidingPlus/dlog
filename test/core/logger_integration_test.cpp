#include <gtest/gtest.h>

#include "logger.h"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>


using namespace dlog;


namespace
{

    class LoggerFileTest : public ::testing::Test
    {
    protected:

        void SetUp() override
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            m_directory = std::filesystem::temp_directory_path() / ("dlog_logger_test_" + std::to_string(suffix));
            std::filesystem::create_directories(m_directory);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_directory, error);
            EXPECT_FALSE(error) << error.message();
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
    auto normalLogger = Logger::BuildLogger<FileFlush>(filename);
    auto rollingLogger = Logger::BuildLogger<RollFileFlush>(basePath, 1024);
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

    EXPECT_THROW(Logger::BuildLogger<FileFlush>(std::filesystem::path{}), std::invalid_argument);
    EXPECT_THROW(Logger::BuildLogger<RollFileFlush>(std::filesystem::path{}, 1024), std::invalid_argument);
    EXPECT_THROW(Logger::BuildLogger<FileFlush>(invalidPath), std::filesystem::filesystem_error);
    EXPECT_THROW(Logger::BuildLogger<RollFileFlush>(invalidPath, 1024), std::filesystem::filesystem_error);
    EXPECT_EQ(Thread::NumCreated(), workersBefore);
    EXPECT_NO_THROW(Logger::BuildLogger<FileFlush>(recovered));
    EXPECT_TRUE(std::filesystem::is_regular_file(recovered));

    std::ifstream input(blocked, std::ios::binary);
    ASSERT_TRUE(input);
    const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(contents, "keep");
}

TEST(LoggerLifetimeTest, LastOwnerAutomaticallyReclaimsWorkerAndOutput)
{
    int constructions = 0;
    int destructions = 0;
    auto value = std::make_unique<int>(42);
    auto logger = Logger::BuildLogger<TrackedFlush>(constructions, destructions, std::move(value));
    EXPECT_EQ(value, nullptr);
    EXPECT_EQ(constructions, 1);
    EXPECT_EQ(destructions, 0);
    auto anotherOwner = logger;
    std::weak_ptr<Logger> observer = logger;
    logger.reset();
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(destructions, 0);

    // 无需显式 stop；弱引用过期且输出析构，验证不存在回调反向强持有日志器的循环。
    anotherOwner.reset();
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(destructions, 1);
}

TEST_F(LoggerFileTest, ExplicitStopDrainsOnlyItsOwnTarget)
{
    const auto firstPath = m_directory / "first.log";
    const auto secondPath = m_directory / "second.log";
    auto first = Logger::BuildLogger<FileFlush>(firstPath);
    auto second = Logger::BuildLogger<FileFlush>(secondPath);

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


TEST_F(LoggerFileTest, LiveMessageKeepsLoggerAliveAndDrainsOnRelease)
{
    const auto path = m_directory / "auto.log";
    auto logger = Logger::BuildLogger<FileFlush>(path);
    std::weak_ptr<Logger> observer = logger;
    {
        auto message = logger->message("app.cpp", 42, LogLevel::INFO);
        logger.reset();
        EXPECT_FALSE(observer.expired());
        message.stream() << "last record";
    }
    EXPECT_TRUE(observer.expired());
    EXPECT_NE(readFile(path).find("[app.cpp:42] last record\n"), std::string::npos);
}


TEST_F(LoggerFileTest, ConcurrentRecordsStayWholeAcrossBufferBoundaries)
{
    const auto path = m_directory / "concurrent.log";
    auto logger = Logger::BuildLogger<FileFlush>(path);
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
    auto logger = Logger::BuildLogger<FileFlush>(path);

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
    auto logger = Logger::BuildLogger<FailingFlush>(calls);
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
}


TEST(LoggerMessageTest, DefaultStdoutColorsTheLevel)
{
    auto logger = Logger::BuildLogger();
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
    auto logger = Logger::BuildLogger<ColoredFlush>(text);
    logger->message("app.cpp", 42, LogLevel::INFO).stream() << "custom record";
    logger->stop();

    EXPECT_NE(text.find("\x1b["), std::string::npos);
    EXPECT_NE(text.find("[app.cpp:42] custom record\n"), std::string::npos);
}


TEST_F(LoggerFileTest, MessagePreservesSavedErrno)
{
    const auto path = m_directory / "errno.log";
    Logger::Ptr logger = Logger::BuildLogger<FileFlush>(path);
    logger->message("app.cpp", 42, LogLevel::ERROR, EACCES).stream() << "open failed";
    logger->stop();

    const auto errorText = std::error_code(EACCES, std::generic_category()).message();
    const auto expected = "[ERROR] [app.cpp:42] " + errorText + " (errno=" + std::to_string(EACCES) + ") open failed\n";
    EXPECT_NE(readFile(path).find(expected), std::string::npos);
}

TEST_F(LoggerFileTest, FatalAndFollowingRecordsAreWrittenOnStop)
{
    const auto path = m_directory / "fatal.log";
    auto logger = Logger::BuildLogger<FileFlush>(path);
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
        auto logger = Logger::BuildLogger<RollFileFlush>(base, 1024);
        logger->message("app.cpp", 1, LogLevel::INFO).stream() << "first run";
        logger->stop();
    }
    {
        auto logger = Logger::BuildLogger<RollFileFlush>(base, 1024);
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
    auto logger = Logger::BuildLogger<FileFlush>("/dev/full");
    logger->message("app.cpp", 1, LogLevel::ERROR).stream() << "write failure";
    EXPECT_THROW(logger->stop(), std::exception);
}
#endif
