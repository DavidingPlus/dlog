#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "logstream.h"


using namespace dlog;


namespace
{

    std::string_view streamContents(const LogStream &stream)
    {
        const FlexibleBuffer &buffer = stream.buffer();
        return {buffer.readableBegin(), buffer.readableBytes()};
    }

} // namespace


TEST(LogStreamTest, StartsEmptyAndIsNonCopyable)
{
    LogStream stream;

    EXPECT_EQ(stream.buffer().readableBytes(), 0u);
    EXPECT_EQ(stream.buffer().writableBytes(), FlexibleBuffer::kInitialSize);
    EXPECT_FALSE(std::is_copy_constructible_v<LogStream>);
    EXPECT_FALSE(std::is_copy_assignable_v<LogStream>);
    EXPECT_FALSE(std::is_move_constructible_v<LogStream>);
    EXPECT_FALSE(std::is_move_assignable_v<LogStream>);
}

TEST(LogStreamTest, OperatorOverloadsReturnTheSameStream)
{
    LogStream stream;

    LogStream &result = stream << 42 << ' ' << "answer";

    EXPECT_EQ(&result, &stream);
    EXPECT_EQ(streamContents(stream), "42 answer");
}

TEST(LogStreamTest, FormatsBooleanValues)
{
    LogStream stream;

    stream << true << ' ' << false;

    EXPECT_EQ(streamContents(stream), "true false");
}

TEST(LogStreamTest, FormatsDoubleWithTwelveSignificantDigits)
{
    LogStream stream;

    stream << 3.141592653589793;

    EXPECT_EQ(streamContents(stream), "3.14159265359");
}

TEST(LogStreamTest, FormatsFloatThroughDoubleOverload)
{
    LogStream stream;

    stream << 1.25f << ' ' << -0.5f;

    EXPECT_EQ(streamContents(stream), "1.25 -0.5");
}

TEST(LogStreamTest, FormatsIntegralValuesAcrossAllOverloads)
{
    const short shortValue = std::numeric_limits<short>::min();
    const unsigned short unsignedShortValue = std::numeric_limits<unsigned short>::max();
    const int intValue = std::numeric_limits<int>::min();
    const unsigned int unsignedIntValue = std::numeric_limits<unsigned int>::max();
    const long longValue = std::numeric_limits<long>::min();
    const unsigned long unsignedLongValue = std::numeric_limits<unsigned long>::max();
    const long long longLongValue = std::numeric_limits<long long>::min();
    const unsigned long long unsignedLongLongValue = std::numeric_limits<unsigned long long>::max();

    LogStream stream;

    stream << shortValue << '|'
           << unsignedShortValue << '|'
           << intValue << '|'
           << unsignedIntValue << '|'
           << longValue << '|'
           << unsignedLongValue << '|'
           << longLongValue << '|'
           << unsignedLongLongValue;

    const std::string expected =
        std::to_string(shortValue) + "|" +
        std::to_string(unsignedShortValue) + "|" +
        std::to_string(intValue) + "|" +
        std::to_string(unsignedIntValue) + "|" +
        std::to_string(longValue) + "|" +
        std::to_string(unsignedLongValue) + "|" +
        std::to_string(longLongValue) + "|" +
        std::to_string(unsignedLongLongValue);

    EXPECT_EQ(streamContents(stream), expected);
}

TEST(LogStreamTest, FormatsZeroAndPositiveIntegralValues)
{
    LogStream stream;

    stream << static_cast<short>(0) << ' '
           << static_cast<unsigned short>(0) << ' '
           << 0 << ' '
           << 0u << ' '
           << 0L << ' '
           << 0UL << ' '
           << 0LL << ' '
           << 0ULL << ' '
           << 123 << ' '
           << 456ULL;

    EXPECT_EQ(streamContents(stream), "0 0 0 0 0 0 0 0 123 456");
}

TEST(LogStreamTest, FormatsLargeDoubleValues)
{
    LogStream stream;

    stream << std::numeric_limits<double>::max();

    const std::string result(streamContents(stream));
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find('e'), std::string::npos);
}

TEST(LogStreamTest, FormatsStringViewWithEmbeddedNull)
{
    const char data[] = {'a', '\0', 'b'};
    LogStream stream;

    stream << std::string_view(data, sizeof(data));

    EXPECT_EQ(streamContents(stream), std::string(data, sizeof(data)));
}

TEST(LogStreamTest, FormatsStringViewSlice)
{
    const std::string text = "prefix-message-suffix";
    LogStream stream;

    stream << std::string_view(text).substr(7, 7);

    EXPECT_EQ(streamContents(stream), "message");
}

TEST(LogStreamTest, FormatsCharacterAndNullCharacter)
{
    LogStream stream;

    stream << 'a' << '\0' << 'b';

    const char expectedData[] = {'a', '\0', 'b'};
    const std::string expected(expectedData, sizeof(expectedData));
    EXPECT_EQ(streamContents(stream), expected);
}

TEST(LogStreamTest, CharacterOverloadsReuseStringViewFormatting)
{
    const unsigned char unsignedText[] = {'u', 'n', 's', 'i', 'g', 'n', 'e', 'd', '\0'};
    LogStream stream;

    stream << 'A' << "bc" << unsignedText;

    EXPECT_EQ(streamContents(stream), "Abcunsigned");
}

TEST(LogStreamTest, FormatsStdStringWithEmbeddedNull)
{
    const char data[] = {'a', '\0', 'b'};
    const std::string text(data, sizeof(data));
    LogStream stream;

    stream << text;

    EXPECT_EQ(streamContents(stream), text);
}

TEST(LogStreamTest, EmptyStringValuesDoNotChangeTheBuffer)
{
    LogStream stream;

    stream << "prefix";
    const auto lengthBefore = stream.buffer().readableBytes();

    stream << "" << std::string() << std::string_view();

    EXPECT_EQ(stream.buffer().readableBytes(), lengthBefore);
    EXPECT_EQ(streamContents(stream), "prefix");
}

TEST(LogStreamTest, NullCharacterPointersAreIgnored)
{
    const char *text = nullptr;
    const unsigned char *unsignedText = nullptr;
    LogStream stream;

    stream << text << unsignedText;

    EXPECT_TRUE(streamContents(stream).empty());
}

TEST(LogStreamTest, ResetBufferClearsDataAndAllowsReuse)
{
    LogStream stream;

    stream << "old value";
    stream.resetBuffer();

    EXPECT_EQ(stream.buffer().readableBytes(), 0u);
    EXPECT_EQ(stream.buffer().writableBytes(), FlexibleBuffer::kInitialSize);
    EXPECT_TRUE(streamContents(stream).empty());

    stream << "new value";

    EXPECT_EQ(streamContents(stream), "new value");
}

TEST(LogStreamTest, GrowsWhenAccumulatedFieldsExceedInitialCapacity)
{
    LogStream stream;
    const std::string payload(FlexibleBuffer::kInitialSize - 2, 'x');
    stream << std::string_view(payload);
    ASSERT_EQ(stream.buffer().writableBytes(), 2u);

    stream << "123" << '|' << true << '|' << 1.25;

    EXPECT_EQ(streamContents(stream), payload + "123|true|1.25");
}

// 回归原复现场景：正文、前后字段和换行都必须保留。
TEST(LogStreamTest, PreservesOversizedPayloadAndSurroundingFields)
{
    LogStream stream;
    const std::string payload(5000, 'A');

    stream << "payload=" << payload << ", done\n";

    EXPECT_EQ(streamContents(stream), "payload=" + payload + ", done\n");
}

TEST(LogStreamTest, GrowsWhenFormattedIntegerExceedsRemainingSpace)
{
    LogStream stream;
    const std::string payload(FlexibleBuffer::kInitialSize - 1, 'x');
    stream << payload;
    ASSERT_EQ(stream.buffer().writableBytes(), 1u);

    stream << 12345;

    EXPECT_EQ(streamContents(stream), payload + "12345");
}

TEST(LogStreamTest, AppendsNewlineAfterExactlyFillingInitialCapacity)
{
    LogStream stream;
    const std::string payload(FlexibleBuffer::kInitialSize, 'x');
    stream << payload;
    ASSERT_EQ(stream.buffer().writableBytes(), 0u);

    stream << '\n';

    EXPECT_EQ(streamContents(stream), payload + "\n");
}

TEST(LogStreamTest, ExplicitLengthAppendPreservesLongBinaryData)
{
    LogStream stream;
    std::string payload(5000, 'A');
    payload[2500] = '\0';

    stream.append(payload.data(), payload.size());
    stream << ", done\n";

    EXPECT_EQ(streamContents(stream), payload + ", done\n");
}

TEST(LogStreamTest, ResetAfterGrowthAllowsReuseWithoutOldContent)
{
    LogStream stream;
    stream << std::string(5000, 'A');
    const size_t storageSize = stream.buffer().readableBytes() + stream.buffer().writableBytes();

    stream.resetBuffer();
    EXPECT_EQ(stream.buffer().readableBytes(), 0u);
    EXPECT_EQ(stream.buffer().writableBytes(), storageSize);
    stream << "new" << 42;

    EXPECT_EQ(streamContents(stream), "new42");
}
