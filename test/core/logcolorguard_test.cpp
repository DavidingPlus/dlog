#include <gtest/gtest.h>

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "logger.h"
#include "logcolorguard.h"


namespace
{

    struct LogColorExample
    {
        LogLevel level;
        const char *name;
        const char *expected;
    };

    constexpr std::array<LogColorExample, 6> kLogColorExamples{{
        {LogLevel::TRACE, "TRACE", "\x1b[37mTRACE\x1b[0m"},
        {LogLevel::DEBUG, "DEBUG", "\x1b[36mDEBUG\x1b[0m"},
        {LogLevel::INFO, "INFO", "\x1b[32mINFO\x1b[0m"},
        {LogLevel::WARN, "WARN", "\x1b[33m\x1b[1mWARN\x1b[0m"},
        {LogLevel::ERROR, "ERROR", "\x1b[31m\x1b[1mERROR\x1b[0m"},
        {LogLevel::FATAL, "FATAL", "\x1b[1m\x1b[41mFATAL\x1b[0m"},
    }};

    static_assert(!std::is_copy_constructible_v<LogColorGuard>);
    static_assert(!std::is_copy_assignable_v<LogColorGuard>);
    static_assert(!std::is_move_constructible_v<LogColorGuard>);
    static_assert(!std::is_move_assignable_v<LogColorGuard>);

} // namespace


TEST(LogColorTest, WritesExpectedAnsiSequenceForEveryLogLevel)
{
    for (const LogColorExample &example : kLogColorExamples)
    {
        LogStream stream;
        {
            LogColorGuard color(stream, example.level);
            stream << example.name;
        }

        EXPECT_EQ(stream.buffer().toString(), example.expected) << example.name;
    }
}

TEST(LogColorTest, AppliesColorAtConstructionAndResetsAfterDestruction)
{
    LogStream stream;
    {
        LogColorGuard color(stream, LogLevel::INFO);

        EXPECT_EQ(stream.buffer().toString(), "\x1b[32m");

        stream << "message";
        EXPECT_EQ(stream.buffer().toString(), "\x1b[32mmessage");
    }

    EXPECT_EQ(stream.buffer().toString(), "\x1b[32mmessage\x1b[0m");
}

TEST(LogColorTest, WritesColorAndResetOnlyToTheSuppliedStream)
{
    LogStream targetStream;
    LogStream otherStream;
    {
        LogColorGuard color(targetStream, LogLevel::WARN);
        targetStream << "target";
        otherStream << "other";
    }

    EXPECT_EQ(targetStream.buffer().toString(), "\x1b[33m\x1b[1mtarget\x1b[0m");
    EXPECT_EQ(otherStream.buffer().toString(), "other");
}

TEST(LogColorTest, ResetsColorDuringStackUnwinding)
{
    LogStream stream;

    try
    {
        LogColorGuard color(stream, LogLevel::ERROR);
        stream << "before exception";
        throw std::runtime_error("test exception");
    }
    catch (const std::runtime_error &)
    {
    }

    EXPECT_EQ(stream.buffer().toString(), "\x1b[31m\x1b[1mbefore exception\x1b[0m");
}
