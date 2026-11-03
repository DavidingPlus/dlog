#include <gtest/gtest.h>

#include <string>

#include "buffer.h"


using namespace dlog;


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

    const std::string payload(5000, 'A');
    buffer.append(payload.data(), payload.size());

    EXPECT_EQ(buffer.readableBytes(), payload.size());
    EXPECT_EQ(buffer.retrieveAllAsString(), payload);
}
