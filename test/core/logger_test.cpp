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

    std::string captureOutput(const std::function<void()> &writeLog, LogLevelColorMode colorMode = LogLevelColorMode::OFF)
    {
        std::string output;

        // 旧格式测试验证纯文本布局；颜色行为由单独的 LoggerColorTest 覆盖。
        Logger::SetOutput([&output](const char *data, size_t len)
                          { output.append(data, len); },
                          colorMode);

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
                      { output.append(data, len); },
                      LogLevelColorMode::OFF);

    {
        Logger logger(__FILE__, 123, LogLevel::INFO);
        logger.stream() << "scope message";

        EXPECT_TRUE(output.empty());
    }

    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("[INFO] [logger_test.cpp:123] scope message\n"), std::string::npos);

    restoreDefaultOutput();
}

TEST(LoggerTest, FormatsTimestampLevelMessageSourceAndLine)
{
    const std::string output = captureOutput([]
                                             { Logger(__FILE__, 456, LogLevel::INFO).stream() << "hello"; });

    const std::regex pattern(R"(^\[\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{6}\] \[INFO\] \[logger_test\.cpp:456\] hello\n$)");

    EXPECT_TRUE(std::regex_match(output, pattern)) << output;
}

TEST(LoggerTest, FormatsEveryNonFatalLevel)
{
    const std::array<std::pair<LogLevel, const char *>, 5> levels{{
        {LogLevel::TRACE, "TRACE"},
        {LogLevel::DEBUG, "DEBUG"},
        {LogLevel::INFO, "INFO"},
        {LogLevel::WARN, "WARN"},
        {LogLevel::ERROR, "ERROR"},
    }};

    for (const auto &[level, logLevelName] : levels)
    {
        const std::string output = captureOutput([&]
                                                 { Logger(__FILE__, 789, level).stream() << "payload"; });

        const std::string expected = "[" + std::string(logLevelName) + "] [logger_test.cpp:789] payload\n";
        EXPECT_NE(output.find(expected), std::string::npos) << output;
    }
}

TEST(LoggerMacroTest, QDebugStyleMacroFormatsAndOutputsOnFullExpressionEnd)
{
    const std::string output = captureOutput([]
                                             { DLOG_INFO() << "started " << 42; });

    EXPECT_NE(output.find("[INFO] [logger_test.cpp:"), std::string::npos);
    EXPECT_NE(output.find("] started 42\n"), std::string::npos);
    EXPECT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
}

TEST(LoggerMacroTest, SupportsAllListedMacros)
{
    const std::array<std::pair<const char *, std::function<void()>>, 5> loggers{{
        {"TRACE", []
         { DLOG_TRACE() << "message"; }},
        {"DEBUG", []
         { DLOG_DEBUG() << "message"; }},
        {"INFO", []
         { DLOG_INFO() << "message"; }},
        {"WARN", []
         { DLOG_WARN() << "message"; }},
        {"ERROR", []
         { DLOG_ERROR() << "message"; }},
    }};

    for (const auto &[logLevelName, writeLog] : loggers)
    {
        const std::string output = captureOutput(writeLog);
        EXPECT_NE(output.find("[" + std::string(logLevelName) + "] [logger_test.cpp:"), std::string::npos) << output;
        EXPECT_NE(output.find("] message\n"), std::string::npos) << output;
    }
}

TEST(LoggerOutputTest, PreservesEmbeddedNullCharactersThroughExplicitLength)
{
    constexpr char message[] = {'l', 'e', 'f', 't', '\0', 'r', 'i', 'g', 'h', 't'};

    const std::string output = captureOutput([&]
                                             { DLOG_INFO() << std::string_view(message, sizeof(message)); });

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
        callbackData.assign(data, len); },
                      LogLevelColorMode::OFF);

    DLOG_INFO() << "length check";

    EXPECT_EQ(callbackLength, callbackData.size());
    EXPECT_NE(callbackData.find("[INFO] [logger_test.cpp:"), std::string::npos);
    EXPECT_NE(callbackData.find("] length check\n"), std::string::npos);

    restoreDefaultOutput();
}

TEST(LoggerColorOutputTest, EmitsColorByDefaultForEveryNonFatalLevel)
{
    std::string output;

    Logger::SetOutput([&output](const char *data, size_t len)
                      { output.append(data, len); });

    DLOG_TRACE() << "colored";
    DLOG_DEBUG() << "colored";
    DLOG_INFO() << "colored";
    DLOG_WARN() << "colored";
    DLOG_ERROR() << "colored";

    const std::array<const char *, 5> expectedColors{{
        "\x1b[37m[TRACE] \x1b[0m",
        "\x1b[36m[DEBUG] \x1b[0m",
        "\x1b[32m[INFO] \x1b[0m",
        "\x1b[33m\x1b[1m[WARN] \x1b[0m",
        "\x1b[31m\x1b[1m[ERROR] \x1b[0m",
    }};

    for (const char *expectedColor : expectedColors)
    {
        EXPECT_NE(output.find(expectedColor), std::string::npos) << output;
    }

    restoreDefaultOutput();
}

TEST(LoggerColorOutputTest, OmitsColorWhenModeIsOff)
{
    const std::string output = captureOutput([]
                                             { DLOG_INFO() << "plain"; },
                                             LogLevelColorMode::OFF);

    EXPECT_NE(output.find("[INFO] [logger_test.cpp:"), std::string::npos);
    EXPECT_NE(output.find("] plain\n"), std::string::npos);
    EXPECT_EQ(output.find('\x1b'), std::string::npos);
}

TEST(LoggerOutputTest, DoesNotFlushNormalLogs)
{
    bool flushed = false;
    Logger::SetFlush([&flushed]
                     { flushed = true; });

    const std::string output = captureOutput([]
                                             { DLOG_INFO() << "normal"; });

    EXPECT_NE(output.find("[INFO] [logger_test.cpp:"), std::string::npos);
    EXPECT_NE(output.find("] normal\n"), std::string::npos);
    EXPECT_FALSE(flushed);

    restoreDefaultFlush();
}

TEST(LoggerDeathTest, FatalLogWritesColorBeforeAborting)
{
    EXPECT_DEATH(
        {
            Logger::SetOutput([](const char *data, size_t len)
                              { std::fwrite(data, sizeof(char), len, stderr); });
            Logger::SetFlush([]
                             { std::fflush(stderr); });
            DLOG_FATAL() << "fatal message";
        },
        "\x1b\\[1m\x1b\\[41m\\[FATAL\\] \x1b\\[0m.*fatal message");
}
