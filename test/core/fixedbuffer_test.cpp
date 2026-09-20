#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

#include "fixedbuffer.h"


namespace
{

    using TestBuffer = FixedBuffer<8>;

} // namespace


TEST(FixedBufferSizeTest, DefinesExpectedSmallAndLargeCapacities)
{
    EXPECT_EQ(kSmallBufferSize, 4000u);
    EXPECT_EQ(kLargeBufferSize, 4000000u);
    EXPECT_EQ(kLargeBufferSize, kSmallBufferSize * 1000u);
}


TEST(FixedBufferSizeTest, ConstantsCanBeUsedAsTemplateArguments)
{
    using SmallBuffer = FixedBuffer<kSmallBufferSize>;
    using LargeBuffer = FixedBuffer<kLargeBufferSize>;

    SmallBuffer smallBuffer;
    auto largeBuffer = std::make_unique<LargeBuffer>();

    EXPECT_EQ(smallBuffer.avail(), kSmallBufferSize);
    EXPECT_EQ(largeBuffer->avail(), kLargeBufferSize);
}


TEST(FixedBufferSizeTest, SmallBufferUsesSmallCapacity)
{
    FixedBuffer<kSmallBufferSize> buffer;

    buffer.append("small", 5);

    EXPECT_EQ(buffer.length(), 5u);
    EXPECT_EQ(buffer.avail(), kSmallBufferSize - 5u);
    EXPECT_EQ(buffer.toString(), "small");
}


TEST(FixedBufferSizeTest, LargeBufferUsesLargeCapacityAndCanBeReused)
{
    using LargeBuffer = FixedBuffer<kLargeBufferSize>;
    auto buffer = std::make_unique<LargeBuffer>();

    buffer->append("large", 5);
    EXPECT_EQ(buffer->length(), 5u);
    EXPECT_EQ(buffer->avail(), kLargeBufferSize - 5u);
    EXPECT_EQ(buffer->toString(), "large");

    buffer->reset();
    EXPECT_EQ(buffer->length(), 0u);
    EXPECT_EQ(buffer->avail(), kLargeBufferSize);
}


TEST(FixedBufferTest, StartsEmpty)
{
    TestBuffer buffer;

    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.avail(), 8u);
    EXPECT_EQ(buffer.current(), buffer.data());
    EXPECT_TRUE(buffer.toString().empty());
}

TEST(FixedBufferTest, DataPointsToTheBeginningAndCurrentPointsToNextWritePosition)
{
    TestBuffer buffer;
    const char payload[] = "abc";

    const char *begin = buffer.data();
    buffer.append(payload, 3);

    EXPECT_EQ(buffer.data(), begin);
    EXPECT_EQ(buffer.current(), begin + 3);
    EXPECT_EQ(buffer.length(), 3);
    EXPECT_EQ(buffer.avail(), 5u);
}

TEST(FixedBufferTest, AppendsOnePayload)
{
    TestBuffer buffer;
    const char payload[] = "hello";

    buffer.append(payload, 5);

    EXPECT_EQ(buffer.length(), 5);
    EXPECT_EQ(buffer.avail(), 3u);
    EXPECT_EQ(buffer.toString(), "hello");
    EXPECT_EQ(std::memcmp(buffer.data(), payload, 5), 0);
}

TEST(FixedBufferTest, AppendsMultiplePayloadsContiguously)
{
    TestBuffer buffer;

    buffer.append("ab", 2);
    buffer.append("cde", 3);
    buffer.append("f", 1);

    EXPECT_EQ(buffer.length(), 6);
    EXPECT_EQ(buffer.avail(), 2u);
    EXPECT_EQ(buffer.toString(), "abcdef");
}

TEST(FixedBufferTest, AcceptsPayloadThatExactlyFillsRemainingSpace)
{
    TestBuffer buffer;

    buffer.append("123", 3);
    buffer.append("45678", 5);

    EXPECT_EQ(buffer.length(), 8);
    EXPECT_EQ(buffer.avail(), 0u);
    EXPECT_EQ(buffer.current(), buffer.data() + 8);
    EXPECT_EQ(buffer.toString(), "12345678");
}

TEST(FixedBufferTest, RejectsPayloadLargerThanRemainingSpaceWithoutPartialWrite)
{
    TestBuffer buffer;

    buffer.append("abc", 3);
    const char *currentBefore = buffer.current();
    const int lengthBefore = buffer.length();
    const size_t availBefore = buffer.avail();

    buffer.append("123456", 6);

    EXPECT_EQ(buffer.current(), currentBefore);
    EXPECT_EQ(buffer.length(), lengthBefore);
    EXPECT_EQ(buffer.avail(), availBefore);
    EXPECT_EQ(buffer.toString(), "abc");
}

TEST(FixedBufferTest, RejectsAppendWhenBufferIsFull)
{
    TestBuffer buffer;

    buffer.append("12345678", 8);
    const char *currentBefore = buffer.current();
    const int lengthBefore = buffer.length();

    buffer.append("x", 1);

    EXPECT_EQ(buffer.current(), currentBefore);
    EXPECT_EQ(buffer.length(), lengthBefore);
    EXPECT_EQ(buffer.avail(), 0u);
    EXPECT_EQ(buffer.toString(), "12345678");
}

TEST(FixedBufferTest, ZeroLengthAppendDoesNotChangeState)
{
    TestBuffer buffer;

    buffer.append("abc", 3);
    const char *currentBefore = buffer.current();
    const int lengthBefore = buffer.length();
    const size_t availBefore = buffer.avail();

    buffer.append("ignored", 0);

    EXPECT_EQ(buffer.current(), currentBefore);
    EXPECT_EQ(buffer.length(), lengthBefore);
    EXPECT_EQ(buffer.avail(), availBefore);
    EXPECT_EQ(buffer.toString(), "abc");
}

TEST(FixedBufferTest, PreservesExplicitLengthAndEmbeddedNullCharacters)
{
    FixedBuffer<5> buffer;
    const char payload[] = {'a', '\0', 'b', '\0', 'c'};
    const std::string expected(payload, sizeof(payload));

    buffer.append(payload, sizeof(payload));

    EXPECT_EQ(buffer.length(), 5);
    EXPECT_EQ(buffer.toString(), expected);
    EXPECT_EQ(std::memcmp(buffer.data(), payload, sizeof(payload)), 0);
}

TEST(FixedBufferTest, ResetClearsLogicalStateAndAllowsReuse)
{
    TestBuffer buffer;
    const char *begin = buffer.data();

    buffer.append("old", 3);
    buffer.reset();

    EXPECT_EQ(buffer.data(), begin);
    EXPECT_EQ(buffer.current(), begin);
    EXPECT_EQ(buffer.length(), 0);
    EXPECT_EQ(buffer.avail(), 8u);
    EXPECT_TRUE(buffer.toString().empty());

    buffer.append("new", 3);

    EXPECT_EQ(buffer.current(), begin + 3);
    EXPECT_EQ(buffer.length(), 3);
    EXPECT_EQ(buffer.toString(), "new");
}

TEST(FixedBufferTest, BzeroClearsPhysicalBytesWithoutChangingLogicalState)
{
    TestBuffer buffer;
    const std::array<char, 8> zeros{};

    buffer.append("abcd", 4);
    const char *currentBefore = buffer.current();

    buffer.bzero();

    EXPECT_EQ(buffer.current(), currentBefore);
    EXPECT_EQ(buffer.length(), 4);
    EXPECT_EQ(buffer.avail(), 4u);
    EXPECT_EQ(buffer.toString(), std::string(4, '\0'));
    EXPECT_EQ(std::memcmp(buffer.data(), zeros.data(), zeros.size()), 0);
}

TEST(FixedBufferTest, BzeroCanBeFollowedByAppendingMoreData)
{
    TestBuffer buffer;

    buffer.append("abc", 3);
    buffer.bzero();
    buffer.append("xyz", 3);

    EXPECT_EQ(buffer.length(), 6);
    EXPECT_EQ(buffer.toString(), std::string("\0\0\0xyz", 6));
}

TEST(FixedBufferTest, IsNonCopyableAndNonMovable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<TestBuffer>);
    EXPECT_FALSE(std::is_copy_assignable_v<TestBuffer>);
    EXPECT_FALSE(std::is_move_constructible_v<TestBuffer>);
    EXPECT_FALSE(std::is_move_assignable_v<TestBuffer>);
}
