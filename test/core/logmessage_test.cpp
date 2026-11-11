#include <gtest/gtest.h>

#include "logmessage.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>


using namespace dlog;


TEST(LogMessageTest, SubmitsLongBinaryRecordOnceAtScopeEnd)
{
    static_assert(!std::is_copy_constructible_v<LogMessage>);
    static_assert(!std::is_move_constructible_v<LogMessage>);
    static_assert(std::is_nothrow_destructible_v<LogMessage>);

    std::string payload(5000, 'R');
    payload[2500] = '\0';
    std::string output;
    size_t outputCalls = 0;

    {
        LogMessage record(
            "dir/record.cpp", 42, LogLevel::INFO, 0,
            [&](const char *data, size_t len)
            {
                ++outputCalls;
                output.append(data, len);
            },
            LogLevelColorMode::OFF);
        record.stream() << std::string_view(payload.data(), payload.size());
        EXPECT_EQ(outputCalls, 0u);
    }

    EXPECT_EQ(outputCalls, 1u);
    const std::string expected = "[INFO ] [record.cpp:42] " + payload + '\n';
    ASSERT_GE(output.size(), expected.size());
    EXPECT_EQ(output.substr(output.size() - expected.size()), expected);
    EXPECT_EQ(output.find('\x1b'), std::string::npos);
}

TEST(LogMessageTest, RejectsEmptyCallbackBeforeARecordCanBeUsed)
{
    EXPECT_THROW((LogMessage(
                     "record.cpp", 1, LogLevel::INFO, 0, {}, LogLevelColorMode::OFF)),
                 std::invalid_argument);
}

TEST(LogMessageDeathTest, OutputFailureIsReportedWithoutTerminatingTheCaller)
{
    EXPECT_EXIT(
        {
            {
                LogMessage record(
                    "record.cpp", 1, LogLevel::INFO, 0,
                    [](const char *, size_t)
                    { throw std::runtime_error("output failed"); },
                    LogLevelColorMode::OFF);
                record.stream() << "message";
            }
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "dlog: log message submission failed");
}

class LogMessageFormatTest : public ::testing::TestWithParam<std::tuple<std::tuple<LogLevel, const char *>, LogLevelColorMode>>
{
};


TEST_P(LogMessageFormatTest, FormatsLevelAndLimitsColorToTheLevel)
{
    const auto [levelAndLabel, color] = GetParam();
    const auto [level, label] = levelAndLabel;
    std::string output;
    size_t calls = 0;
    {
        LogMessage record(
            R"(dir\sub/record.cpp)", 123, level, 0,
            [&](const char *data, size_t len)
            { ++calls; output.assign(data, len); },
            color);
        record.stream() << "body";
    }
    EXPECT_EQ(calls, 1u);
    if (color == LogLevelColorMode::ON)
    {
        EXPECT_EQ(output.find("\x1b["), 29u);
        const auto reset = output.find("\x1b[0m");
        ASSERT_NE(reset, std::string::npos);
        EXPECT_EQ(output.substr(reset + 4), "[record.cpp:123] body\n");
        output = std::regex_replace(output, std::regex("\x1b\\[[0-9;]*m"), "");
    }
    else
        EXPECT_EQ(output.find('\x1b'), std::string::npos);
    const std::string suffix = std::string(label) + "[record.cpp:123] body\n";
    ASSERT_GE(output.size(), suffix.size());
    EXPECT_EQ(output.substr(output.size() - suffix.size()), suffix);
    EXPECT_TRUE(std::regex_match(output.substr(0, 29),
                                 std::regex(R"(^\[\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}\.\d{6}\] $)")))
        << output;
}

INSTANTIATE_TEST_SUITE_P(
    AllLevels, LogMessageFormatTest,
    ::testing::Combine(
        ::testing::Values(
            std::make_tuple(LogLevel::TRACE, "[TRACE] "),
            std::make_tuple(LogLevel::DEBUG, "[DEBUG] "),
            std::make_tuple(LogLevel::INFO, "[INFO ] "),
            std::make_tuple(LogLevel::WARN, "[WARN ] "),
            std::make_tuple(LogLevel::ERROR, "[ERROR] "),
            std::make_tuple(LogLevel::FATAL, "[FATAL] ")),
        ::testing::Values(LogLevelColorMode::OFF, LogLevelColorMode::ON)));

TEST(LogMessageTest, EmptyBodyStillSubmitsOneCompleteRecord)
{
    std::string output;
    size_t calls = 0;
    {
        LogMessage record(
            "empty.cpp", 0, LogLevel::INFO, 0,
            [&](const char *data, size_t len)
            { ++calls; output.assign(data, len); },
            LogLevelColorMode::OFF);
        EXPECT_TRUE(output.empty());
    }
    EXPECT_EQ(calls, 1u);
    const std::string suffix = "[INFO ] [empty.cpp:0] \n";
    ASSERT_GE(output.size(), suffix.size());
    EXPECT_EQ(output.substr(output.size() - suffix.size()), suffix);
    EXPECT_EQ(output.find("errno="), std::string::npos);
}

TEST(LogMessageTest, SavedErrnoIsIndependentOfLaterErrnoChanges)
{
    const int previousErrno = errno;
    std::string output;
    {
        LogMessage record(
            "error.cpp", 7, LogLevel::ERROR, EINVAL,
            [&](const char *data, size_t len)
            { output.assign(data, len); },
            LogLevelColorMode::OFF);
        errno = ENOENT;
        record.stream() << "failed";
    }
    errno = previousErrno;
    const std::string suffix = "[ERROR] [error.cpp:7] " +
                               std::error_code(EINVAL, std::generic_category()).message() +
                               " (errno=" + std::to_string(EINVAL) + ") failed\n";
    ASSERT_GE(output.size(), suffix.size());
    EXPECT_EQ(output.substr(output.size() - suffix.size()), suffix);
}

TEST(LogMessageTest, OverlappingRecordsKeepTheirOwnBodyAndBoundCallback)
{
    std::string first, second;
    LogMessage::OutputFunc output = [&](const char *data, size_t len)
    { first.assign(data, len); };
    {
        LogMessage outer(
            "outer.cpp", 1, LogLevel::INFO, 0, output, LogLevelColorMode::OFF);
        outer.stream() << "outer before ";
        output = [&](const char *data, size_t len)
        { second.assign(data, len); };
        {
            LogMessage inner(
                "inner.cpp", 2, LogLevel::DEBUG, 0, output, LogLevelColorMode::OFF);
            inner.stream() << "inner";
        }
        EXPECT_TRUE(first.empty());
        EXPECT_NE(second.find("[inner.cpp:2] inner\n"), std::string::npos);
        outer.stream() << "outer after";
    }
    EXPECT_NE(first.find("[outer.cpp:1] outer before outer after\n"), std::string::npos);
    EXPECT_EQ(first.find("inner"), std::string::npos);
    EXPECT_EQ(second.find("outer"), std::string::npos);
}

TEST(LogMessageTest, SubmitsWhileUnwindingWithoutReplacingTheOriginalException)
{
    std::string output;
    bool caught = false;
    try
    {
        LogMessage record(
            "unwind.cpp", 3, LogLevel::WARN, 0,
            [&](const char *data, size_t len)
            { output.assign(data, len); },
            LogLevelColorMode::OFF);
        record.stream() << "before exception";
        throw std::runtime_error("original exception");
    }
    catch (const std::runtime_error &error)
    {
        caught = true;
        EXPECT_STREQ(error.what(), "original exception");
    }
    EXPECT_TRUE(caught);
    EXPECT_NE(output.find("[unwind.cpp:3] before exception\n"), std::string::npos);
}

TEST(LogMessageDeathTest, NonStandardOutputExceptionDuringUnwindingPreservesOriginalException)
{
    EXPECT_EXIT(
        {
            try
            {
                LogMessage record(
                    "unwind.cpp", 4, LogLevel::ERROR, 0,
                    [](const char *, size_t)
                    { throw 42; },
                    LogLevelColorMode::OFF);
                throw std::runtime_error("original");
            }
            catch (const std::runtime_error &error)
            {
                std::_Exit(std::string(error.what()) == "original" ? 0 : 1);
            }
            std::_Exit(2);
        },
        ::testing::ExitedWithCode(0), "dlog: log message submission failed");
}

TEST(LogMessageTest, FatalSubmitsOnceWithoutTerminating)
{
    std::string text;
    int outputs = 0;
    {
        LogMessage record("fatal.cpp", 9, LogLevel::FATAL, 0,
                          [&](const char *data, size_t len) { ++outputs; text.assign(data, len); },
                          LogLevelColorMode::OFF);
        record.stream() << "fatal body";
    }
    EXPECT_EQ(outputs, 1);
    EXPECT_NE(text.find("[FATAL] [fatal.cpp:9] fatal body\n"), std::string::npos);
}
