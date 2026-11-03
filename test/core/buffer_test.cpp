#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "buffer.h"


using namespace dlog;


TEST(BufferTest, DefaultConstructionProvidesExpectedWritableSpace)
{
    Buffer buffer;

    EXPECT_EQ(Buffer::kInitialSize, 1024u);
    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), Buffer::kInitialSize);
    EXPECT_EQ(buffer.peek(), buffer.beginWrite());
}

TEST(BufferTest, StartsAtStorageBeginningAndUsesAllInitialSpace)
{
    Buffer buffer(8);
    const char *start = buffer.peek();

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), 8u);
    EXPECT_EQ(buffer.beginWrite(), start);

    buffer.append("abcdefgh", 8);

    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.writableBytes(), 0u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "abcdefgh");
    EXPECT_EQ(buffer.beginWrite(), start);
    EXPECT_EQ(buffer.writableBytes(), 8u);
}

TEST(BufferTest, ReclaimsConsumedSpaceAtStorageBeginningWithoutGrowing)
{
    Buffer buffer(8);
    const char *start = buffer.peek();
    buffer.append("abcdef", 6);
    buffer.retrieve(4);

    buffer.append("WXYZ", 4);

    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.readableBytes(), 6u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "efWXYZ");
}

TEST(BufferTest, ZeroInitialSizeSupportsEmptyOperationsAndGrowth)
{
    Buffer buffer(0);
    const Buffer &view = buffer;

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), 0u);
    EXPECT_EQ(buffer.beginWrite(), buffer.peek());
    EXPECT_EQ(view.beginWrite(), view.peek());
    buffer.ensureWritableBytes(0);
    buffer.append(nullptr, 0);
    buffer.retrieveAll();
    EXPECT_TRUE(buffer.retrieveAllAsString().empty());
    EXPECT_TRUE(buffer.retrieveAsString(std::numeric_limits<size_t>::max()).empty());

    const std::string payload(5000, 'A');
    buffer.append(payload.data(), payload.size());

    EXPECT_EQ(buffer.readableBytes(), payload.size());
    EXPECT_EQ(buffer.retrieveAllAsString(), payload);
}

TEST(BufferTest, RetrieveAsStringConsumesOnlyRequestedReadableBytes)
{
    Buffer buffer(8);
    buffer.append("abcdef", 6);
    const char *start = buffer.peek();

    EXPECT_TRUE(buffer.retrieveAsString(0).empty());
    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.readableBytes(), 6u);

    EXPECT_EQ(buffer.retrieveAsString(2), "ab");
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.peek(), start + 2);
    EXPECT_EQ(buffer.retrieveAllAsString(), "cdef");
}

TEST(BufferTest, RetrieveAsStringAtOrBeyondReadableLengthConsumesAllAndResets)
{
    for (size_t length : {size_t{4}, size_t{5}, std::numeric_limits<size_t>::max()})
    {
        SCOPED_TRACE(length);
        Buffer buffer(8);
        const char *start = buffer.peek();
        buffer.append("xxabcd", 6);
        buffer.retrieve(2);

        EXPECT_EQ(buffer.retrieveAsString(length), "abcd");
        EXPECT_EQ(buffer.readableBytes(), 0u);
        EXPECT_EQ(buffer.writableBytes(), 8u);
        EXPECT_EQ(buffer.peek(), start);
        EXPECT_EQ(buffer.beginWrite(), start);

        buffer.append("new", 3);
        EXPECT_EQ(buffer.retrieveAllAsString(), "new");
    }
}

TEST(BufferTest, RejectsPositiveLengthNullSourceWithoutChangingState)
{
    Buffer buffer(8);
    buffer.append("xxdata", 6);
    buffer.retrieve(2);
    const char *start = buffer.peek();

    EXPECT_NO_THROW(buffer.append(nullptr, 0));
    EXPECT_THROW(buffer.append(nullptr, 1), std::invalid_argument);
    EXPECT_THROW(buffer.append(nullptr, 100), std::invalid_argument);

    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "data");
}

TEST(BufferTest, RejectsOversizedGrowthAndAppendWithoutChangingState)
{
    Buffer buffer(8);
    buffer.append("xxdata", 6);
    buffer.retrieve(2);
    const char *start = buffer.peek();
    const size_t oversized = std::numeric_limits<size_t>::max();

    ASSERT_THROW(buffer.ensureWritableBytes(oversized), std::length_error);
    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "data");

    ASSERT_THROW(buffer.append("x", oversized), std::length_error);
    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "data");
}

// 对照 FixedBuffer：检查显式长度、连续追加及读写位置。
TEST(BufferTest, AppendsExplicitLengthWithoutRequiringNullTerminator)
{
    Buffer buffer(8);
    const char payload[] = {'h', 'e', 'l', 'l', 'o'};
    const char *start = buffer.peek();

    buffer.append(payload, sizeof(payload));

    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.readableBytes(), sizeof(payload));
    EXPECT_EQ(buffer.writableBytes(), 3u);
    EXPECT_EQ(std::memcmp(buffer.peek(), payload, sizeof(payload)), 0);
    EXPECT_EQ(buffer.retrieveAllAsString(), "hello");
}

TEST(BufferTest, AppendsMultiplePayloadsContiguously)
{
    Buffer buffer(8);
    buffer.append("ab", 2);
    buffer.append("cde", 3);
    buffer.append("f", 1);

    EXPECT_EQ(buffer.readableBytes(), 6u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "abcdef");
}

TEST(BufferTest, ReadAndWritePointersTrackDifferentPositions)
{
    Buffer buffer(8);
    const char *start = buffer.peek();
    buffer.append("abcdef", 6);
    buffer.retrieve(2);
    const Buffer &view = buffer;

    EXPECT_EQ(buffer.peek(), start + 2);
    EXPECT_EQ(buffer.beginWrite(), start + 6);
    EXPECT_EQ(view.beginWrite(), buffer.beginWrite());
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
}

TEST(BufferTest, PreservesExplicitLengthAndEmbeddedNullCharacters)
{
    Buffer buffer(2);
    const char payload[] = {'a', '\0', 'b', '\0', 'c'};
    buffer.append(payload, sizeof(payload));

    EXPECT_EQ(buffer.readableBytes(), sizeof(payload));
    EXPECT_EQ(std::memcmp(buffer.peek(), payload, sizeof(payload)), 0);
    EXPECT_EQ(buffer.retrieveAsString(3), std::string(payload, 3));
    EXPECT_EQ(buffer.retrieveAllAsString(), std::string(payload + 3, 2));
}

TEST(BufferTest, ZeroLengthAppendPreservesExistingDataAndPointers)
{
    Buffer buffer(8);
    buffer.append("abcdef", 6);
    buffer.retrieve(2);
    const char *read = buffer.peek();
    const char *write = buffer.beginWrite();

    buffer.append("ignored", 0);
    buffer.append(nullptr, 0);

    EXPECT_EQ(buffer.peek(), read);
    EXPECT_EQ(buffer.beginWrite(), write);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "cdef");
}

// Buffer 应增长并保留全部数据，不能沿用 FixedBuffer 的超限拒绝期望。
TEST(BufferTest, GrowsForPayloadLargerThanRemainingSpaceWithoutLosingPrefix)
{
    Buffer buffer(8);
    buffer.append("abc", 3);
    buffer.append("123456", 6);

    EXPECT_EQ(buffer.readableBytes(), 9u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "abc123456");
}

TEST(BufferTest, GrowsWhenAppendingToAFullBuffer)
{
    Buffer buffer(8);
    buffer.append("123", 3);
    buffer.append("45678", 5);
    ASSERT_EQ(buffer.writableBytes(), 0u);

    buffer.append("x", 1);

    EXPECT_EQ(buffer.readableBytes(), 9u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "12345678x");
}

TEST(BufferTest, RetrieveZeroAndPartialLengthOnlyMovesReadPosition)
{
    Buffer buffer(8);
    buffer.append("abcdef", 6);
    const char *start = buffer.peek();

    buffer.retrieve(0);
    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.readableBytes(), 6u);

    buffer.retrieve(2);
    EXPECT_EQ(buffer.peek(), start + 2);
    EXPECT_EQ(buffer.beginWrite(), start + 6);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "cdef");
}

TEST(BufferTest, RetrieveAtOrBeyondReadableLengthResetsBothPositions)
{
    for (size_t length : {size_t{4}, size_t{5}, std::numeric_limits<size_t>::max()})
    {
        SCOPED_TRACE(length);
        Buffer buffer(8);
        const char *start = buffer.peek();
        buffer.append("xxdata", 6);
        buffer.retrieve(2);

        buffer.retrieve(length);

        EXPECT_EQ(buffer.readableBytes(), 0u);
        EXPECT_EQ(buffer.writableBytes(), 8u);
        EXPECT_EQ(buffer.peek(), start);
        EXPECT_EQ(buffer.beginWrite(), start);
        buffer.retrieve(length);
        EXPECT_EQ(buffer.writableBytes(), 8u);
    }
}

TEST(BufferTest, RetrieveAllCanBeRepeatedAndFollowedByAppend)
{
    Buffer buffer(8);
    const char *start = buffer.peek();
    buffer.append("abcdef", 6);
    buffer.retrieve(2);

    buffer.retrieveAll();
    buffer.retrieveAll();

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), 8u);
    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.beginWrite(), start);
    buffer.append("new", 3);
    EXPECT_EQ(buffer.retrieveAllAsString(), "new");
}

TEST(BufferTest, ResetRetainsGrownStorageAndAllowsReuse)
{
    Buffer buffer(2);
    buffer.append("abcdef", 6);
    buffer.retrieve(2);
    const char *start = buffer.peek() - 2;
    const size_t storageSize = 6 + buffer.writableBytes();

    buffer.reset();
    buffer.reset();

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), storageSize);
    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.beginWrite(), start);
    buffer.append("new", 3);
    EXPECT_EQ(buffer.retrieveAllAsString(), "new");
}

TEST(BufferTest, EnsureWritableWithinAvailableSpaceDoesNotChangeState)
{
    Buffer buffer(8);
    const char *start = buffer.peek();
    buffer.append("abc", 3);

    buffer.ensureWritableBytes(0);
    buffer.ensureWritableBytes(5);

    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.beginWrite(), start + 3);
    EXPECT_EQ(buffer.readableBytes(), 3u);
    EXPECT_EQ(buffer.writableBytes(), 5u);
    EXPECT_EQ(buffer.retrieveAllAsString(), "abc");
}

TEST(BufferTest, EnsureWritableReclaimsExactlyEnoughConsumedSpace)
{
    Buffer buffer(8);
    const char *start = buffer.peek();
    buffer.append("abcdef", 6);
    buffer.retrieve(3);

    buffer.ensureWritableBytes(5);

    EXPECT_EQ(buffer.peek(), start);
    EXPECT_EQ(buffer.beginWrite(), start + 3);
    EXPECT_EQ(buffer.readableBytes(), 3u);
    EXPECT_EQ(buffer.writableBytes(), 5u);
    buffer.append("12345", 5);
    EXPECT_EQ(buffer.retrieveAllAsString(), "def12345");
}

TEST(BufferTest, EnsureWritableGrowsWhenReclaimedSpaceWouldBeInsufficient)
{
    Buffer buffer(8);
    buffer.append("abcdefgh", 8);
    buffer.retrieve(3);

    buffer.ensureWritableBytes(6);

    EXPECT_EQ(buffer.readableBytes(), 5u);
    EXPECT_GE(buffer.writableBytes(), 6u);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "defgh");
    buffer.append("123456", 6);
    EXPECT_EQ(buffer.retrieveAllAsString(), "defgh123456");
}

// 新 Buffer 允许值拷贝；验证非零读索引及副本增长不会影响原对象。
TEST(BufferTest, CopyConstructionPreservesReadPositionAndOwnsIndependentStorage)
{
    EXPECT_TRUE(std::is_copy_constructible_v<Buffer>);
    Buffer original(8);
    original.append("xxdata", 6);
    original.retrieve(2);

    Buffer copy(original);
    EXPECT_NE(copy.peek(), original.peek());
    EXPECT_EQ(copy.readableBytes(), original.readableBytes());
    EXPECT_EQ(copy.writableBytes(), original.writableBytes());
    copy.append("-copy", 5);
    copy.retrieve(1);

    EXPECT_EQ(copy.retrieveAllAsString(), "ata-copy");
    EXPECT_EQ(original.retrieveAllAsString(), "data");
}

TEST(BufferTest, CopyAssignmentReplacesExistingDataAndKeepsStorageIndependent)
{
    EXPECT_TRUE(std::is_copy_assignable_v<Buffer>);
    Buffer original(8);
    original.append("xxdata", 6);
    original.retrieve(2);
    Buffer copy(16);
    copy.append("old", 3);

    copy = original;
    EXPECT_NE(copy.peek(), original.peek());
    EXPECT_EQ(copy.readableBytes(), 4u);
    EXPECT_EQ(copy.writableBytes(), original.writableBytes());
    original.reset();
    original.append("other", 5);

    EXPECT_EQ(copy.retrieveAllAsString(), "data");
    EXPECT_EQ(original.retrieveAllAsString(), "other");
}

TEST(BufferTest, RejectsUnrepresentableInitialSize)
{
    EXPECT_THROW(Buffer{std::numeric_limits<size_t>::max()}, std::length_error);
}
