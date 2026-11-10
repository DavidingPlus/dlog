#include <gtest/gtest.h>

#include "logmessage.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>


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
    size_t flushCalls = 0;

    {
        LogMessage record(
            "dir/record.cpp", 42, LogLevel::INFO, 0,
            [&](const char *data, size_t len)
            {
                ++outputCalls;
                output.append(data, len);
            },
            [&]
            { ++flushCalls; },
            LogLevelColorMode::OFF);
        record.stream() << std::string_view(payload.data(), payload.size());
        EXPECT_EQ(outputCalls, 0u);
    }

    EXPECT_EQ(outputCalls, 1u);
    EXPECT_EQ(flushCalls, 0u);
    const std::string expected = "[INFO ] [record.cpp:42] " + payload + '\n';
    ASSERT_GE(output.size(), expected.size());
    EXPECT_EQ(output.substr(output.size() - expected.size()), expected);
    EXPECT_EQ(output.find('\x1b'), std::string::npos);
}

TEST(LogMessageTest, RejectsEmptyCallbacksBeforeARecordCanBeUsed)
{
    EXPECT_THROW((LogMessage(
                     "record.cpp", 1, LogLevel::INFO, 0,
                     {}, [] {}, LogLevelColorMode::OFF)),
                 std::invalid_argument);
    EXPECT_THROW((LogMessage(
                     "record.cpp", 1, LogLevel::INFO, 0,
                     [](const char *, size_t) {}, {}, LogLevelColorMode::OFF)),
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
                    [] {}, LogLevelColorMode::OFF);
                record.stream() << "message";
            }
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "dlog: log message submission failed");
}

TEST(LogMessageDeathTest, FatalStillFlushesAndAbortsAfterOutputFailure)
{
    EXPECT_DEATH(
        {
            LogMessage record(
                "record.cpp", 1, LogLevel::FATAL, 0,
                [](const char *, size_t)
                { throw std::runtime_error("output failed"); },
                []
                {
                    std::fputs("original target flushed", stderr);
                    std::fflush(stderr);
                },
                LogLevelColorMode::OFF);
            record.stream() << "fatal";
        },
        "original target flushed");
}

TEST(LogMessageDeathTest, FatalReportsFlushFailureBeforeAborting)
{
    EXPECT_DEATH(
        {
            LogMessage record(
                "record.cpp", 1, LogLevel::FATAL, 0,
                [](const char *, size_t) {},
                []
                { throw std::runtime_error("flush failed"); },
                LogLevelColorMode::OFF);
            record.stream() << "fatal";
        },
        "dlog: fatal log flush failed");
}
