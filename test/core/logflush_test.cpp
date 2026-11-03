#include <gtest/gtest.h>

#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

#include "logflush.h"


using namespace dlog;


TEST(StdoutFlushTest, WritesCompleteBinaryDataAndRespectsExplicitLength)
{
    StdoutFlush output;
    LogFlush &target = output;
    std::string payload(5000, 'A');
    payload[2500] = '\0';
    const char tail[] = {'t', 'a', 'i', 'l', '!'};

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(target.writeAndFlush("prefix|", 7));
    EXPECT_NO_THROW(target.writeAndFlush(payload.data(), payload.size()));
    EXPECT_NO_THROW(target.writeAndFlush(tail, 4));
    const std::string actual = testing::internal::GetCapturedStdout();

    EXPECT_EQ(actual, "prefix|" + payload + "tail");
}

TEST(StdoutFlushTest, EmptyWritesProduceNoOutput)
{
    StdoutFlush output;

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(output.writeAndFlush(nullptr, 0));
    EXPECT_NO_THROW(output.writeAndFlush("ignored", 0));
    const std::string actual = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(actual.empty());
}

TEST(StdoutFlushTest, RejectsNullDataWithPositiveLength)
{
    StdoutFlush output;

    EXPECT_THROW(output.writeAndFlush(nullptr, 1), std::invalid_argument);
}

TEST(StdoutFlushTest, RejectsLengthThatCannotBeRepresentedByStreamsize)
{
    const size_t maxLength = static_cast<size_t>(std::numeric_limits<std::streamsize>::max());
    if (maxLength == std::numeric_limits<size_t>::max()) GTEST_SKIP();

    StdoutFlush output;
    const char data = 'x';
    EXPECT_THROW(output.writeAndFlush(&data, maxLength + 1), std::length_error);
}

TEST(StdoutFlushTest, ReportsFailedStreamState)
{
    StdoutFlush output;
    const auto oldState = std::cout.rdstate();
    std::cout.setstate(std::ios::badbit);

    EXPECT_THROW(output.writeAndFlush("x", 1), std::runtime_error);
    EXPECT_NO_THROW(output.writeAndFlush(nullptr, 0));

    std::cout.clear(oldState);
}

TEST(StdoutFlushTest, FlushesNonemptyWritesAndIgnoresEmptyWrites)
{
    class CountingBuffer : public std::stringbuf
    {
    public:
        int flushes = 0;
        int sync() override
        {
            ++flushes;
            return std::stringbuf::sync();
        }
    } buffer;

    StdoutFlush output;
    auto *oldBuffer = std::cout.rdbuf(&buffer);
    EXPECT_NO_THROW(output.writeAndFlush("data", 4));
    EXPECT_NO_THROW(output.writeAndFlush(nullptr, 0));
    EXPECT_NO_THROW(output.writeAndFlush("ignored", 0));
    std::cout.rdbuf(oldBuffer);

    EXPECT_EQ(buffer.str(), "data");
    EXPECT_EQ(buffer.flushes, 1);
}
