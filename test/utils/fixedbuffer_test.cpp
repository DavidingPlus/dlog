#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "fixedbuffer.h"


using namespace dlog;


namespace
{

    using TestBuffer = FixedBuffer<8>;

} // namespace


TEST(FixedBufferSizeTest, DefinesExpectedSmallAndLargeCapacities)
{
    EXPECT_EQ(kSmallFixedBufferSize, 4000u);
    EXPECT_EQ(kLargeFixedBufferSize, 4000000u);
    EXPECT_EQ(kLargeFixedBufferSize, kSmallFixedBufferSize * 1000u);
}


TEST(FixedBufferSizeTest, NamedAliasesUseExpectedCapacities)
{
    SmallFixedBuffer smallBuffer;
    auto largeBuffer = std::make_unique<LargeFixedBuffer>();

    EXPECT_EQ(smallBuffer.writableBytes(), kSmallFixedBufferSize);
    EXPECT_EQ(largeBuffer->writableBytes(), kLargeFixedBufferSize);
}


TEST(FixedBufferSizeTest, SmallFixedBufferUsesSmallCapacity)
{
    SmallFixedBuffer buffer;

    buffer.append("small", 5);

    EXPECT_EQ(buffer.readableBytes(), 5u);
    EXPECT_EQ(buffer.writableBytes(), kSmallFixedBufferSize - 5u);
    EXPECT_EQ(buffer.toString(), "small");
}


TEST(FixedBufferSizeTest, LargeFixedBufferUsesLargeCapacityAndCanBeReused)
{
    auto buffer = std::make_unique<LargeFixedBuffer>();

    buffer->append("large", 5);
    EXPECT_EQ(buffer->readableBytes(), 5u);
    EXPECT_EQ(buffer->writableBytes(), kLargeFixedBufferSize - 5u);
    EXPECT_EQ(buffer->toString(), "large");

    buffer->clear();
    EXPECT_EQ(buffer->readableBytes(), 0u);
    EXPECT_EQ(buffer->writableBytes(), kLargeFixedBufferSize);
}


TEST(FixedBufferTest, StartsEmpty)
{
    TestBuffer buffer;

    EXPECT_EQ(buffer.readableBytes(), 0);
    EXPECT_EQ(buffer.writableBytes(), 8u);
    EXPECT_EQ(buffer.writableBegin(), buffer.readableBegin());
    EXPECT_TRUE(buffer.toString().empty());
}

TEST(FixedBufferTest, ReadableBeginPointsToDataAndWritableBeginPointsToNextWritePosition)
{
    TestBuffer buffer;
    const char payload[] = "abc";

    const char *begin = buffer.readableBegin();
    buffer.append(payload, 3);

    EXPECT_EQ(buffer.readableBegin(), begin);
    EXPECT_EQ(buffer.writableBegin(), begin + 3);
    EXPECT_EQ(buffer.readableBytes(), 3);
    EXPECT_EQ(buffer.writableBytes(), 5u);
}

TEST(FixedBufferTest, AppendsOnePayload)
{
    TestBuffer buffer;
    const char payload[] = "hello";

    buffer.append(payload, 5);

    EXPECT_EQ(buffer.readableBytes(), 5);
    EXPECT_EQ(buffer.writableBytes(), 3u);
    EXPECT_EQ(buffer.toString(), "hello");
    EXPECT_EQ(std::memcmp(buffer.readableBegin(), payload, 5), 0);
}

TEST(FixedBufferTest, AppendsMultiplePayloadsContiguously)
{
    TestBuffer buffer;

    buffer.append("ab", 2);
    buffer.append("cde", 3);
    buffer.append("f", 1);

    EXPECT_EQ(buffer.readableBytes(), 6);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.toString(), "abcdef");
}

TEST(FixedBufferTest, AcceptsPayloadThatExactlyFillsRemainingSpace)
{
    TestBuffer buffer;

    buffer.append("123", 3);
    buffer.append("45678", 5);

    EXPECT_EQ(buffer.readableBytes(), 8);
    EXPECT_EQ(buffer.writableBytes(), 0u);
    EXPECT_EQ(buffer.writableBegin(), buffer.readableBegin() + 8);
    EXPECT_EQ(buffer.toString(), "12345678");
}

TEST(FixedBufferTest, RejectsPayloadLargerThanRemainingSpaceWithoutPartialWrite)
{
    TestBuffer buffer;

    buffer.append("abc", 3);
    const char *currentBefore = buffer.writableBegin();
    const size_t lengthBefore = buffer.readableBytes();
    const size_t availBefore = buffer.writableBytes();

    EXPECT_THROW(buffer.append("123456", 6), std::length_error);

    EXPECT_EQ(buffer.writableBegin(), currentBefore);
    EXPECT_EQ(buffer.readableBytes(), lengthBefore);
    EXPECT_EQ(buffer.writableBytes(), availBefore);
    EXPECT_EQ(buffer.toString(), "abc");
}

TEST(FixedBufferTest, RejectsAppendWhenBufferIsFull)
{
    TestBuffer buffer;

    buffer.append("12345678", 8);
    const char *currentBefore = buffer.writableBegin();
    const size_t lengthBefore = buffer.readableBytes();

    EXPECT_THROW(buffer.append("x", 1), std::length_error);

    EXPECT_EQ(buffer.writableBegin(), currentBefore);
    EXPECT_EQ(buffer.readableBytes(), lengthBefore);
    EXPECT_EQ(buffer.writableBytes(), 0u);
    EXPECT_EQ(buffer.toString(), "12345678");
}

TEST(FixedBufferTest, ZeroLengthAppendDoesNotChangeState)
{
    TestBuffer buffer;

    buffer.append("abc", 3);
    const char *currentBefore = buffer.writableBegin();
    const size_t lengthBefore = buffer.readableBytes();
    const size_t availBefore = buffer.writableBytes();

    buffer.append("ignored", 0);
    EXPECT_NO_THROW(buffer.append(nullptr, 0));

    EXPECT_EQ(buffer.writableBegin(), currentBefore);
    EXPECT_EQ(buffer.readableBytes(), lengthBefore);
    EXPECT_EQ(buffer.writableBytes(), availBefore);
    EXPECT_EQ(buffer.toString(), "abc");
}

TEST(FixedBufferTest, PreservesExplicitLengthAndEmbeddedNullCharacters)
{
    FixedBuffer<5> buffer;
    const char payload[] = {'a', '\0', 'b', '\0', 'c'};
    const std::string expected(payload, sizeof(payload));

    buffer.append(payload, sizeof(payload));

    EXPECT_EQ(buffer.readableBytes(), 5);
    EXPECT_EQ(buffer.toString(), expected);
    EXPECT_EQ(std::memcmp(buffer.readableBegin(), payload, sizeof(payload)), 0);
}

TEST(FixedBufferTest, ClearResetsLogicalStateAndAllowsReuse)
{
    TestBuffer buffer;
    const char *begin = buffer.readableBegin();

    buffer.append("old", 3);
    buffer.clear();

    EXPECT_EQ(buffer.readableBegin(), begin);
    EXPECT_EQ(buffer.writableBegin(), begin);
    EXPECT_EQ(buffer.readableBytes(), 0);
    EXPECT_EQ(buffer.writableBytes(), 8u);
    EXPECT_TRUE(buffer.toString().empty());

    buffer.append("new", 3);

    EXPECT_EQ(buffer.writableBegin(), begin + 3);
    EXPECT_EQ(buffer.readableBytes(), 3);
    EXPECT_EQ(buffer.toString(), "new");
}

TEST(FixedBufferTest, BzeroClearsPhysicalBytesWithoutChangingLogicalState)
{
    TestBuffer buffer;
    const std::array<char, 8> zeros{};

    buffer.append("abcd", 4);
    const char *currentBefore = buffer.writableBegin();

    buffer.bzero();

    EXPECT_EQ(buffer.writableBegin(), currentBefore);
    EXPECT_EQ(buffer.readableBytes(), 4);
    EXPECT_EQ(buffer.writableBytes(), 4u);
    EXPECT_EQ(buffer.toString(), std::string(4, '\0'));
    EXPECT_EQ(std::memcmp(buffer.readableBegin(), zeros.data(), zeros.size()), 0);
}

TEST(FixedBufferTest, BzeroCanBeFollowedByAppendingMoreData)
{
    TestBuffer buffer;

    buffer.append("abc", 3);
    buffer.bzero();
    buffer.append("xyz", 3);

    EXPECT_EQ(buffer.readableBytes(), 6);
    EXPECT_EQ(buffer.toString(), std::string("\0\0\0xyz", 6));
}

TEST(FixedBufferTest, IsNonCopyableAndNonMovable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<TestBuffer>);
    EXPECT_FALSE(std::is_copy_assignable_v<TestBuffer>);
    EXPECT_FALSE(std::is_move_constructible_v<TestBuffer>);
    EXPECT_FALSE(std::is_move_assignable_v<TestBuffer>);
}
