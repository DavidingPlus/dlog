#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <regex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "logger.h"


namespace
{

    std::string_view basenameOf(std::string_view path)
    {
        auto separator = path.find_last_of("/\\");
        return std::string_view::npos == separator ? path : path.substr(separator + 1);
    }

    void restoreDefaultOutput()
    {
        Logger::SetOutput([](const char *data, size_t len)
                          { std::fwrite(data, sizeof(char), len, stdout); });
    }

    void restoreDefaultFlush()
    {
        Logger::SetFlush([]
                         { std::fflush(stdout); });
    }

    std::string captureOutput(const std::function<void()> &writeLog)
    {
        std::string output;

        Logger::SetOutput([&output](const char *data, size_t len)
                          { output.append(data, len); });

        writeLog();

        restoreDefaultOutput();


        return output;
    }

} // namespace


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

TEST(LoggerTest, DoesNotOutputUntilLoggerIsDestroyed)
{
    std::string output;

    Logger::SetOutput([&output](const char *data, size_t len)
                      { output.append(data, len); });

    {
        Logger logger(__FILE__, 123, Logger::LogLevel::INFO);
        logger.stream() << "scope message";

        EXPECT_TRUE(output.empty());
    }

    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("INFO scope message - logger_test.cpp:123\n"), std::string::npos);

    restoreDefaultOutput();
}

TEST(LoggerTest, FormatsTimestampLevelMessageSourceAndLine)
{
    const std::string output = captureOutput([]
                                             { Logger(__FILE__, 456, Logger::LogLevel::INFO).stream() << "hello"; });

    const std::regex pattern(R"(^\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{6} INFO hello - logger_test\.cpp:456\n$)");

    EXPECT_TRUE(std::regex_match(output, pattern)) << output;
}

TEST(LoggerTest, FormatsEveryNonFatalLevel)
{
    const std::array<std::pair<Logger::LogLevel, const char *>, 5> levels{{
        {Logger::LogLevel::TRACE, "TRACE"},
        {Logger::LogLevel::DEBUG, "DEBUG"},
        {Logger::LogLevel::INFO, "INFO"},
        {Logger::LogLevel::WARN, "WARN"},
        {Logger::LogLevel::ERROR, "ERROR"},
    }};

    for (const auto &[level, levelName] : levels)
    {
        const std::string output = captureOutput([&]
                                                 { Logger(__FILE__, 789, level).stream() << "payload"; });

        const std::string expected = std::string(levelName) + " payload - logger_test.cpp:789\n";
        EXPECT_NE(output.find(expected), std::string::npos) << output;
    }
}

TEST(LoggerMacroTest, QDebugStyleMacroFormatsAndOutputsOnFullExpressionEnd)
{
    const std::string output = captureOutput([]
                                             { LOG_INFO() << "started " << 42; });

    EXPECT_NE(output.find("INFO started 42 - logger_test.cpp:"), std::string::npos);
    EXPECT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
}

TEST(LoggerMacroTest, SupportsAllListedMacros)
{
    const std::array<std::pair<const char *, std::function<void()>>, 4> loggers{{
        {"DEBUG", []
         { LOG_DEBUG() << "message"; }},
        {"INFO", []
         { LOG_INFO() << "message"; }},
        {"WARN", []
         { LOG_WARN() << "message"; }},
        {"ERROR", []
         { LOG_ERROR() << "message"; }},
    }};

    for (const auto &[levelName, writeLog] : loggers)
    {
        const std::string output = captureOutput(writeLog);
        EXPECT_NE(output.find(std::string(levelName) + " message - logger_test.cpp:"), std::string::npos) << output;
    }
}

TEST(LoggerOutputTest, PreservesEmbeddedNullCharactersThroughExplicitLength)
{
    constexpr char message[] = {'l', 'e', 'f', 't', '\0', 'r', 'i', 'g', 'h', 't'};

    const std::string output = captureOutput([&]
                                             { LOG_INFO() << std::string_view(message, sizeof(message)); });

    const std::string expected(message, sizeof(message));
    EXPECT_NE(output.find(expected), std::string::npos);
}

TEST(LoggerOutputTest, PassesTheExactBufferLengthToOutputCallback)
{
    size_t callbackLength = 0;
    std::string callbackData;

    Logger::SetOutput([&](const char *data, size_t len)
                      {
        callbackLength = len;
        callbackData.assign(data, len); });

    LOG_INFO() << "length check";

    EXPECT_EQ(callbackLength, callbackData.size());
    EXPECT_NE(callbackData.find("INFO length check - logger_test.cpp:"), std::string::npos);

    restoreDefaultOutput();
}

TEST(LoggerOutputTest, DoesNotFlushNormalLogs)
{
    bool flushed = false;
    Logger::SetFlush([&flushed]
                     { flushed = true; });

    const std::string output = captureOutput([]
                                             { LOG_INFO() << "normal"; });

    EXPECT_NE(output.find("INFO normal - logger_test.cpp:"), std::string::npos);
    EXPECT_FALSE(flushed);

    restoreDefaultFlush();
}

TEST(LoggerDeathTest, FatalLogAbortsTheProcess)
{
    EXPECT_DEATH({ LOG_FATAL() << "fatal message"; }, ".*");
}
