#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "flexiblebuffer.h"
#include "fixedbuffer.h"


using namespace dlog;


TEST(FlexibleBufferTest, DefaultConstructionProvidesExpectedWritableSpace)
{
    FlexibleBuffer buffer;

    EXPECT_EQ(FlexibleBuffer::kInitialSize, 1024u);
    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), FlexibleBuffer::kInitialSize);
    EXPECT_EQ(buffer.readableBegin(), buffer.writableBegin());
}

TEST(FlexibleBufferTest, StorageAndReadableRegionsStayDistinctAfterConsumption)
{
    FlexibleBuffer buffer(8);
    const auto storage = buffer.storageData();
    buffer.append("abcdef", 6);
    buffer.consume(2);

    EXPECT_EQ(buffer.storageData(), storage);
    EXPECT_EQ(buffer.capacity(), 8u);
    EXPECT_EQ(buffer.readableBegin(), storage + 2);
    EXPECT_EQ(buffer.writableBegin(), storage + 6);
    EXPECT_EQ(buffer.toString(), "cdef");
    EXPECT_EQ(buffer.readableBytes(), 4u);

    buffer.append("WXYZ", 4);
    EXPECT_EQ(buffer.storageData(), storage);
    EXPECT_EQ(buffer.capacity(), 8u);
    EXPECT_EQ(buffer.readableBegin(), storage);
    EXPECT_EQ(buffer.toString(), "cdefWXYZ");
}


TEST(FlexibleBufferTest, StartsAtStorageBeginningAndUsesAllInitialSpace)
{
    FlexibleBuffer buffer(8);
    const char *start = buffer.readableBegin();

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), 8u);
    EXPECT_EQ(buffer.writableBegin(), start);

    buffer.append("abcdefgh", 8);

    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.writableBytes(), 0u);
    EXPECT_EQ(buffer.consumeAllAsString(), "abcdefgh");
    EXPECT_EQ(buffer.writableBegin(), start);
    EXPECT_EQ(buffer.writableBytes(), 8u);
}

TEST(FlexibleBufferTest, ReclaimsConsumedSpaceAtStorageBeginningWithoutGrowing)
{
    FlexibleBuffer buffer(8);
    const char *start = buffer.readableBegin();
    buffer.append("abcdef", 6);
    buffer.consume(4);

    buffer.append("WXYZ", 4);

    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.readableBytes(), 6u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.consumeAllAsString(), "efWXYZ");
}

TEST(FlexibleBufferTest, ZeroInitialSizeSupportsEmptyOperationsAndGrowth)
{
    FlexibleBuffer buffer(0);
    const FlexibleBuffer &view = buffer;

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), 0u);
    EXPECT_EQ(buffer.writableBegin(), buffer.readableBegin());
    EXPECT_EQ(view.writableBegin(), view.readableBegin());
    buffer.ensureWritableBytes(0);
    buffer.append(nullptr, 0);
    buffer.clear();
    EXPECT_TRUE(buffer.consumeAllAsString().empty());
    EXPECT_TRUE(buffer.toString().empty());
    EXPECT_EQ(buffer.capacity(), 0u);
    EXPECT_TRUE(buffer.consumeAsString(std::numeric_limits<size_t>::max()).empty());

    const std::string payload(5000, 'A');
    buffer.append(payload.data(), payload.size());

    EXPECT_EQ(buffer.readableBytes(), payload.size());
    EXPECT_EQ(buffer.consumeAllAsString(), payload);
}

TEST(FlexibleBufferTest, ConsumeAsStringConsumesOnlyRequestedReadableBytes)
{
    FlexibleBuffer buffer(8);
    buffer.append("abcdef", 6);
    const char *start = buffer.readableBegin();

    EXPECT_TRUE(buffer.consumeAsString(0).empty());
    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.readableBytes(), 6u);

    EXPECT_EQ(buffer.consumeAsString(2), "ab");
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.readableBegin(), start + 2);
    EXPECT_EQ(buffer.consumeAllAsString(), "cdef");
}

TEST(FlexibleBufferTest, ConsumeAsStringAtOrBeyondReadableLengthConsumesAllAndResets)
{
    for (size_t length : {size_t{4}, size_t{5}, std::numeric_limits<size_t>::max()})
    {
        SCOPED_TRACE(length);
        FlexibleBuffer buffer(8);
        const char *start = buffer.readableBegin();
        buffer.append("xxabcd", 6);
        buffer.consume(2);

        EXPECT_EQ(buffer.consumeAsString(length), "abcd");
        EXPECT_EQ(buffer.readableBytes(), 0u);
        EXPECT_EQ(buffer.writableBytes(), 8u);
        EXPECT_EQ(buffer.readableBegin(), start);
        EXPECT_EQ(buffer.writableBegin(), start);

        buffer.append("new", 3);
        EXPECT_EQ(buffer.consumeAllAsString(), "new");
    }
}

TEST(FlexibleBufferTest, RejectsPositiveLengthNullSourceWithoutChangingState)
{
    FlexibleBuffer buffer(8);
    buffer.append("xxdata", 6);
    buffer.consume(2);
    const char *start = buffer.readableBegin();

    EXPECT_NO_THROW(buffer.append(nullptr, 0));
    EXPECT_THROW(buffer.append(nullptr, 1), std::invalid_argument);
    EXPECT_THROW(buffer.append(nullptr, 100), std::invalid_argument);

    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.consumeAllAsString(), "data");
}

TEST(FlexibleBufferTest, RejectsOversizedGrowthAndAppendWithoutChangingState)
{
    FlexibleBuffer buffer(8);
    buffer.append("xxdata", 6);
    buffer.consume(2);
    const char *start = buffer.readableBegin();
    const size_t oversized = std::numeric_limits<size_t>::max();

    ASSERT_THROW(buffer.ensureWritableBytes(oversized), std::length_error);
    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(std::string(buffer.readableBegin(), buffer.readableBytes()), "data");

    ASSERT_THROW(buffer.append("x", oversized), std::length_error);
    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.consumeAllAsString(), "data");
}

// 对照 FixedBuffer：检查显式长度、连续追加及读写位置。
TEST(FlexibleBufferTest, AppendsExplicitLengthWithoutRequiringNullTerminator)
{
    FlexibleBuffer buffer(8);
    const char payload[] = {'h', 'e', 'l', 'l', 'o'};
    const char *start = buffer.readableBegin();

    buffer.append(payload, sizeof(payload));

    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.readableBytes(), sizeof(payload));
    EXPECT_EQ(buffer.writableBytes(), 3u);
    EXPECT_EQ(std::memcmp(buffer.readableBegin(), payload, sizeof(payload)), 0);
    EXPECT_EQ(buffer.consumeAllAsString(), "hello");
}

TEST(FlexibleBufferTest, AppendsMultiplePayloadsContiguously)
{
    FlexibleBuffer buffer(8);
    buffer.append("ab", 2);
    buffer.append("cde", 3);
    buffer.append("f", 1);

    EXPECT_EQ(buffer.readableBytes(), 6u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.consumeAllAsString(), "abcdef");
}

TEST(FlexibleBufferTest, ReadAndWritePointersTrackDifferentPositions)
{
    FlexibleBuffer buffer(8);
    const char *start = buffer.readableBegin();
    buffer.append("abcdef", 6);
    buffer.consume(2);
    const FlexibleBuffer &view = buffer;

    EXPECT_EQ(buffer.readableBegin(), start + 2);
    EXPECT_EQ(buffer.writableBegin(), start + 6);
    EXPECT_EQ(view.writableBegin(), buffer.writableBegin());
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
}

TEST(FlexibleBufferTest, PreservesExplicitLengthAndEmbeddedNullCharacters)
{
    FlexibleBuffer buffer(2);
    const char payload[] = {'a', '\0', 'b', '\0', 'c'};
    buffer.append(payload, sizeof(payload));

    EXPECT_EQ(buffer.readableBytes(), sizeof(payload));
    EXPECT_EQ(std::memcmp(buffer.readableBegin(), payload, sizeof(payload)), 0);
    EXPECT_EQ(buffer.consumeAsString(3), std::string(payload, 3));
    EXPECT_EQ(buffer.consumeAllAsString(), std::string(payload + 3, 2));
}

TEST(FlexibleBufferTest, ZeroLengthAppendPreservesExistingDataAndPointers)
{
    FlexibleBuffer buffer(8);
    buffer.append("abcdef", 6);
    buffer.consume(2);
    const char *read = buffer.readableBegin();
    const char *write = buffer.writableBegin();

    buffer.append("ignored", 0);
    buffer.append(nullptr, 0);

    EXPECT_EQ(buffer.readableBegin(), read);
    EXPECT_EQ(buffer.writableBegin(), write);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.consumeAllAsString(), "cdef");
}

// FlexibleBuffer 应增长并保留全部数据，不能沿用 FixedBuffer 的超限拒绝期望。
TEST(FlexibleBufferTest, GrowsForPayloadLargerThanRemainingSpaceWithoutLosingPrefix)
{
    FlexibleBuffer buffer(8);
    buffer.append("abc", 3);
    buffer.append("123456", 6);

    EXPECT_EQ(buffer.readableBytes(), 9u);
    EXPECT_EQ(buffer.consumeAllAsString(), "abc123456");
}

TEST(FlexibleBufferTest, GrowsWhenAppendingToAFullBuffer)
{
    FlexibleBuffer buffer(8);
    buffer.append("123", 3);
    buffer.append("45678", 5);
    ASSERT_EQ(buffer.writableBytes(), 0u);

    buffer.append("x", 1);

    EXPECT_EQ(buffer.readableBytes(), 9u);
    EXPECT_EQ(buffer.consumeAllAsString(), "12345678x");
}

TEST(FlexibleBufferTest, ConsumeZeroAndPartialLengthOnlyMovesReadPosition)
{
    FlexibleBuffer buffer(8);
    buffer.append("abcdef", 6);
    const char *start = buffer.readableBegin();

    buffer.consume(0);
    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.readableBytes(), 6u);

    buffer.consume(2);
    EXPECT_EQ(buffer.readableBegin(), start + 2);
    EXPECT_EQ(buffer.writableBegin(), start + 6);
    EXPECT_EQ(buffer.readableBytes(), 4u);
    EXPECT_EQ(buffer.writableBytes(), 2u);
    EXPECT_EQ(buffer.consumeAllAsString(), "cdef");
}

TEST(FlexibleBufferTest, ConsumeAtOrBeyondReadableLengthResetsBothPositions)
{
    for (size_t length : {size_t{4}, size_t{5}, std::numeric_limits<size_t>::max()})
    {
        SCOPED_TRACE(length);
        FlexibleBuffer buffer(8);
        const char *start = buffer.readableBegin();
        buffer.append("xxdata", 6);
        buffer.consume(2);

        buffer.consume(length);

        EXPECT_EQ(buffer.readableBytes(), 0u);
        EXPECT_EQ(buffer.writableBytes(), 8u);
        EXPECT_EQ(buffer.readableBegin(), start);
        EXPECT_EQ(buffer.writableBegin(), start);
        buffer.consume(length);
        EXPECT_EQ(buffer.writableBytes(), 8u);
    }
}

TEST(FlexibleBufferTest, ConsumeAllCanBeRepeatedAndFollowedByAppend)
{
    FlexibleBuffer buffer(8);
    const char *start = buffer.readableBegin();
    buffer.append("abcdef", 6);
    buffer.consume(2);

    buffer.consumeAll();
    buffer.consumeAll();

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), 8u);
    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.writableBegin(), start);
    EXPECT_EQ(buffer.capacity(), 8u);
    EXPECT_EQ(std::string(buffer.storageData(), 6), "abcdef");
    buffer.append("new", 3);
    EXPECT_EQ(buffer.consumeAllAsString(), "new");
}

TEST(FlexibleBufferTest, ClearRetainsGrownStorageAndAllowsReuse)
{
    FlexibleBuffer buffer(2);
    buffer.append("abcdef", 6);
    buffer.consume(2);
    const char *start = buffer.readableBegin() - 2;
    const size_t storageSize = 6 + buffer.writableBytes();

    buffer.clear();
    buffer.clear();

    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), storageSize);
    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.writableBegin(), start);
    buffer.append("new", 3);
    EXPECT_EQ(buffer.consumeAllAsString(), "new");
}

TEST(FlexibleBufferTest, EnsureWritableWithinAvailableSpaceDoesNotChangeState)
{
    FlexibleBuffer buffer(8);
    const char *start = buffer.readableBegin();
    buffer.append("abc", 3);

    buffer.ensureWritableBytes(0);
    buffer.ensureWritableBytes(5);

    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.writableBegin(), start + 3);
    EXPECT_EQ(buffer.readableBytes(), 3u);
    EXPECT_EQ(buffer.writableBytes(), 5u);
    EXPECT_EQ(buffer.consumeAllAsString(), "abc");
}

TEST(FlexibleBufferTest, EnsureWritableReclaimsExactlyEnoughConsumedSpace)
{
    FlexibleBuffer buffer(8);
    const char *start = buffer.readableBegin();
    buffer.append("abcdef", 6);
    buffer.consume(3);

    buffer.ensureWritableBytes(5);

    EXPECT_EQ(buffer.readableBegin(), start);
    EXPECT_EQ(buffer.writableBegin(), start + 3);
    EXPECT_EQ(buffer.readableBytes(), 3u);
    EXPECT_EQ(buffer.writableBytes(), 5u);
    buffer.append("12345", 5);
    EXPECT_EQ(buffer.consumeAllAsString(), "def12345");
}

TEST(FlexibleBufferTest, EnsureWritableGrowsWhenReclaimedSpaceWouldBeInsufficient)
{
    FlexibleBuffer buffer(8);
    buffer.append("abcdefgh", 8);
    buffer.consume(3);

    buffer.ensureWritableBytes(6);

    EXPECT_EQ(buffer.readableBytes(), 5u);
    EXPECT_GE(buffer.writableBytes(), 6u);
    EXPECT_EQ(std::string(buffer.readableBegin(), buffer.readableBytes()), "defgh");
    buffer.append("123456", 6);
    EXPECT_EQ(buffer.consumeAllAsString(), "defgh123456");
}

// 新 FlexibleBuffer 允许值拷贝；验证非零读索引及副本增长不会影响原对象。
TEST(FlexibleBufferTest, CopyConstructionPreservesReadPositionAndOwnsIndependentStorage)
{
    EXPECT_TRUE(std::is_copy_constructible_v<FlexibleBuffer>);
    FlexibleBuffer original(8);
    original.append("xxdata", 6);
    original.consume(2);

    FlexibleBuffer copy(original);
    EXPECT_NE(copy.readableBegin(), original.readableBegin());
    EXPECT_EQ(copy.readableBytes(), original.readableBytes());
    EXPECT_EQ(copy.writableBytes(), original.writableBytes());
    copy.append("-copy", 5);
    copy.consume(1);

    EXPECT_EQ(copy.consumeAllAsString(), "ata-copy");
    EXPECT_EQ(original.consumeAllAsString(), "data");
}

TEST(FlexibleBufferTest, CopyAssignmentReplacesExistingDataAndKeepsStorageIndependent)
{
    EXPECT_TRUE(std::is_copy_assignable_v<FlexibleBuffer>);
    FlexibleBuffer original(8);
    original.append("xxdata", 6);
    original.consume(2);
    FlexibleBuffer copy(16);
    copy.append("old", 3);

    copy = original;
    EXPECT_NE(copy.readableBegin(), original.readableBegin());
    EXPECT_EQ(copy.readableBytes(), 4u);
    EXPECT_EQ(copy.writableBytes(), original.writableBytes());
    original.clear();
    original.append("other", 5);

    EXPECT_EQ(copy.consumeAllAsString(), "data");
    EXPECT_EQ(original.consumeAllAsString(), "other");
}

TEST(FlexibleBufferTest, RejectsUnrepresentableInitialSize)
{
    EXPECT_THROW(FlexibleBuffer{std::numeric_limits<size_t>::max()}, std::length_error);
}


// 同一组测试验证动态和固定缓冲区的共同接口约定。
template <typename BufferType>
class BufferInterfaceTest : public ::testing::Test
{
};

using BufferTypes = ::testing::Types<FlexibleBuffer, FixedBuffer<8>>;
TYPED_TEST_SUITE(BufferInterfaceTest, BufferTypes);


TYPED_TEST(BufferInterfaceTest, SharesReadWriteAndClearSemantics)
{
    TypeParam buffer;
    const TypeParam &view = buffer;
    const auto capacity = buffer.capacity();
    const auto storage = buffer.storageData();
    std::string payload(capacity, 'B');
    payload[1] = '\0';

    EXPECT_EQ(view.storageData(), storage);
    EXPECT_EQ(view.readableBegin(), storage);
    EXPECT_EQ(view.writableBegin(), storage);
    EXPECT_EQ(buffer.writableBytes(), capacity);
    buffer.append(payload.data(), payload.size());

    EXPECT_EQ(buffer.readableBegin(), storage);
    EXPECT_EQ(buffer.readableBytes(), capacity);
    EXPECT_EQ(buffer.writableBytes(), 0u);
    EXPECT_EQ(buffer.writableBegin(), storage + capacity);
    EXPECT_EQ(view.writableBegin(), buffer.writableBegin());
    EXPECT_EQ(buffer.toString(), payload);
    EXPECT_EQ(buffer.readableBytes(), capacity);

    buffer.clear();
    EXPECT_EQ(buffer.capacity(), capacity);
    EXPECT_EQ(buffer.storageData(), storage);
    EXPECT_EQ(buffer.readableBegin(), storage);
    EXPECT_EQ(buffer.writableBegin(), storage);
    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), capacity);
    EXPECT_TRUE(buffer.toString().empty());
    EXPECT_EQ(std::memcmp(buffer.storageData(), payload.data(), capacity), 0);
}

TYPED_TEST(BufferInterfaceTest, RejectsNullSourceWithoutChangingData)
{
    TypeParam buffer;
    buffer.append("old", 3);
    EXPECT_NO_THROW(buffer.append(nullptr, 0));
    EXPECT_THROW(buffer.append(nullptr, 1), std::invalid_argument);
    EXPECT_EQ(buffer.toString(), "old");
    EXPECT_EQ(buffer.readableBytes(), 3u);
    EXPECT_EQ(buffer.writableBegin(), buffer.storageData() + 3);
}

TYPED_TEST(BufferInterfaceTest, ExternalStorageWritesDoNotAdvanceLogicalLength)
{
    TypeParam buffer;
    std::memcpy(buffer.storageData(), "raw", 3);

    EXPECT_EQ(std::memcmp(buffer.storageData(), "raw", 3), 0);
    EXPECT_EQ(buffer.readableBytes(), 0u);
    EXPECT_EQ(buffer.writableBytes(), buffer.capacity());
    EXPECT_EQ(buffer.writableBegin(), buffer.storageData());
    EXPECT_TRUE(buffer.toString().empty());
}
