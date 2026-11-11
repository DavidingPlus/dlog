#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>
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
    auto logger = AsyncLogger::BuildAsyncLogger<RecordingFlush>(text);
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
    auto first = AsyncLogger::BuildAsyncLogger<RecordingFlush>(firstText);
    auto second = AsyncLogger::BuildAsyncLogger<RecordingFlush>(secondText);
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
    auto logger = AsyncLogger::BuildAsyncLogger<RecordingFlush>(text);
    int targets = 0;
    int errors = 0;
    DLOG_LOGGER_SYS_ERROR((++targets, logger), (++errors, 1234567)) << "system error";
    DLOG_LOGGER_SYS_FATAL(logger, 1234568) << "fatal system error";
    EXPECT_THROW(DLOG_LOGGER_INFO(AsyncLogger::Ptr{}) << "invalid", std::invalid_argument);
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
                   text.find("(errno=1234567) default fatal") != std::string::npos ? 0 : 1);
    }, ::testing::ExitedWithCode(0), "");
}

TEST(LogMacroDeathTest, DefaultLoggerDrainsOnNormalExit)
{
    EXPECT_EXIT(
    {
        ::testing::internal::CaptureStdout();
        std::atexit([] {
            auto text = ::testing::internal::GetCapturedStdout();
            std::_Exit(text.find("default exit tail") != std::string::npos ? 0 : 1);
        });
        DLOG_INFO() << "default exit tail";
        std::exit(0);
    }, ::testing::ExitedWithCode(0), "");
}
