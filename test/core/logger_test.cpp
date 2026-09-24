#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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
    EXPECT_NE(output.find("[INFO ] [logger_test.cpp:123] scope message\n"), std::string::npos);

    restoreDefaultOutput();
}

TEST(LoggerTest, FormatsTimestampLevelMessageSourceAndLine)
{
    const std::string output = captureOutput([]
                                             { Logger(__FILE__, 456, LogLevel::INFO).stream() << "hello"; });

    const std::regex pattern(R"(^\[\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{6}\] \[INFO \] \[logger_test\.cpp:456\] hello\n$)");

    EXPECT_TRUE(std::regex_match(output, pattern)) << output;
}

TEST(LoggerTest, FormatsEveryNonFatalLevel)
{
    const std::array<std::pair<LogLevel, const char *>, 5> levels{{
        {LogLevel::TRACE, "TRACE"},
        {LogLevel::DEBUG, "DEBUG"},
        {LogLevel::INFO, "INFO "},
        {LogLevel::WARN, "WARN "},
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

    EXPECT_NE(output.find("[INFO ] [logger_test.cpp:"), std::string::npos);
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
        {"INFO ", []
         { DLOG_INFO() << "message"; }},
        {"WARN ", []
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

TEST(LoggerSystemErrorTest, OmitsSystemErrorWhenSavedErrnoIsZero)
{
    const std::array<std::function<void()>, 2> writeLogs{{
        []
        { Logger(__FILE__, 901, LogLevel::ERROR).stream() << "default errno"; },
        []
        { Logger(__FILE__, 902, LogLevel::ERROR, 0).stream() << "explicit zero errno"; },
    }};

    for (const auto &writeLog : writeLogs)
    {
        const std::string output = captureOutput(writeLog);

        EXPECT_EQ(output.find("(errno="), std::string::npos) << output;
        EXPECT_NE(output.find("[ERROR] [logger_test.cpp:"), std::string::npos) << output;
    }
}

TEST(LoggerSystemErrorTest, ConstructorFormatsTheProvidedErrorNumberAndDescription)
{
    constexpr int savedErrno = EINVAL;
    const std::string errorDescription = std::error_code(savedErrno, std::generic_category()).message();

    const std::string output = captureOutput([savedErrno]
                                             { Logger(__FILE__, 903, LogLevel::ERROR, savedErrno).stream() << "explicit system error"; });

    EXPECT_NE(output.find("[ERROR] [logger_test.cpp:903] "), std::string::npos) << output;
    EXPECT_NE(output.find(errorDescription + " (errno=" + std::to_string(savedErrno) + ") explicit system error\n"), std::string::npos) << output;
}

TEST(LoggerSystemErrorMacroTest, SysErrorUsesErrorLevelAndTheCallerSavedErrno)
{
    const int savedErrno = EACCES;
    errno = ENOENT;
    ASSERT_NE(savedErrno, errno);
    const std::string expectedErrorDescription = std::error_code(savedErrno, std::generic_category()).message();

    const std::string output = captureOutput([savedErrno]
                                             { DLOG_SYS_ERROR(savedErrno) << "open file failed"; });

    EXPECT_NE(output.find("[ERROR] [logger_test.cpp:"), std::string::npos) << output;
    EXPECT_NE(output.find(expectedErrorDescription + " (errno=" + std::to_string(savedErrno) + ") open file failed\n"), std::string::npos) << output;
    EXPECT_EQ(output.find("(errno=" + std::to_string(ENOENT) + ")"), std::string::npos) << output;
}

TEST(LoggerSystemErrorMacroTest, EvaluatesTheSavedErrnoArgumentOnce)
{
    int evaluationCount = 0;

    const std::string output = captureOutput([&]
                                             { DLOG_SYS_ERROR((++evaluationCount, EINVAL)) << "single evaluation"; });

    EXPECT_EQ(evaluationCount, 1);
    EXPECT_NE(output.find("single evaluation\n"), std::string::npos) << output;
}

TEST(LoggerSystemErrorTest, ConcurrentLogsKeepEachErrorNumberWithItsMessage)
{
    constexpr std::array<int, 4> errorNumbers{{EINVAL, EACCES, ENOENT, ERANGE}};
    constexpr size_t logsPerThread = 32;
    std::array<std::string, errorNumbers.size()> errorDescriptions;
    for (size_t i = 0; i < errorNumbers.size(); ++i)
    {
        // 使用与 Logger 相同的错误类别生成期望文本，避免依赖 strerror 的平台措辞。
        errorDescriptions[i] = std::error_code(errorNumbers[i], std::generic_category()).message();
    }

    std::string output;
    std::mutex outputMutex;
    Logger::SetOutput([&](const char *data, size_t len)
                      {
        std::lock_guard<std::mutex> lock(outputMutex);
        output.append(data, len); },
                      LogLevelColorMode::OFF);

    std::array<std::thread, errorNumbers.size()> workers;
    for (size_t workerIndex = 0; workerIndex < workers.size(); ++workerIndex)
    {
        workers[workerIndex] = std::thread([&, workerIndex]
                                           {
            for (size_t messageIndex = 0; messageIndex < logsPerThread; ++messageIndex)
            {
                DLOG_SYS_ERROR(errorNumbers[workerIndex])
                    << "worker=" << workerIndex << " message=" << messageIndex;
            } });
    }

    for (auto &worker : workers)
    {
        worker.join();
    }
    restoreDefaultOutput();

    for (size_t workerIndex = 0; workerIndex < errorNumbers.size(); ++workerIndex)
    {
        for (size_t messageIndex = 0; messageIndex < logsPerThread; ++messageIndex)
        {
            const std::string expectedRecord = errorDescriptions[workerIndex] + " (errno=" + std::to_string(errorNumbers[workerIndex]) + ") worker=" + std::to_string(workerIndex) + " message=" + std::to_string(messageIndex) + "\n";
            EXPECT_NE(output.find(expectedRecord), std::string::npos) << "Missing record: " << expectedRecord;
        }
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
    EXPECT_NE(callbackData.find("[INFO ] [logger_test.cpp:"), std::string::npos);
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
        "\x1b[32m[INFO ] \x1b[0m",
        "\x1b[33m\x1b[1m[WARN ] \x1b[0m",
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

    EXPECT_NE(output.find("[INFO ] [logger_test.cpp:"), std::string::npos);
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

    EXPECT_NE(output.find("[INFO ] [logger_test.cpp:"), std::string::npos);
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

TEST(LoggerDeathTest, SysFatalWritesSavedErrnoFlushesAndAborts)
{
    EXPECT_DEATH(
        {
            Logger::SetOutput([](const char *data, size_t len)
                              {
                // 去掉末尾换行，使死亡断言的匹配内容保持在同一行。
                if (len > 0 && data[len - 1] == '\n') --len;
                std::fwrite(data, sizeof(char), len, stderr); },
                              LogLevelColorMode::OFF);
            Logger::SetFlush([]
                             {
                std::fputs(" <flush-called>", stderr);
                std::fflush(stderr); });
            DLOG_SYS_FATAL(1234567) << "fatal system error";
        },
        R"(\[FATAL\].*\(errno=1234567\) fatal system error <flush-called>)");
}
