#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

#include "timestamp.h"

#include "globalmacros.h"


// 验证默认构造得到的时间戳是 0。
TEST(TimestampTest, DefaultConstructor)
{
    Timestamp ts;

    EXPECT_EQ(ts.microSecondsSinceEpoch(), 0);
    EXPECT_EQ(ts.secondsSinceEpoch(), 0);
}

// 验证指定微秒值能够被完整保存。
TEST(TimestampTest, ExplicitMicroseconds)
{
    constexpr int64_t microseconds = 123456789;
    Timestamp ts(microseconds);

    EXPECT_EQ(ts.microSecondsSinceEpoch(), microseconds);
}

// 验证当前时间戳接近系统当前时间。
TEST(TimestampTest, Now)
{
    Timestamp ts = Timestamp::Now();

    auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto difference = ts.microSecondsSinceEpoch() - now;

    // Timestamp::Now() 与系统当前时间的差值应小于 1 秒。
    EXPECT_GE(difference, -1000000);
    EXPECT_LE(difference, 1000000);
}

// 验证时间戳常量。
TEST(TimestampTest, MicrosecondsPerSecond)
{
    EXPECT_EQ(Timestamp::kMicroSecondsPerSecond, 1000000);
}

// 验证微秒时间戳转换为秒时会截去不足一秒的部分。
TEST(TimestampTest, SecondsSinceEpoch)
{
    EXPECT_EQ(Timestamp(0).secondsSinceEpoch(), 0);
    EXPECT_EQ(Timestamp(1000000).secondsSinceEpoch(), 1);
    EXPECT_EQ(Timestamp(1234567).secondsSinceEpoch(), 1);
    EXPECT_EQ(Timestamp(-1234567).secondsSinceEpoch(), -1);
}

// 验证无效时间戳等价于默认构造的零时间戳。
TEST(TimestampTest, Invalid)
{
    Timestamp invalid = Timestamp::Invalid();
    Timestamp defaultTimestamp;

    EXPECT_EQ(invalid, defaultTimestamp);
    EXPECT_EQ(invalid.microSecondsSinceEpoch(), 0);
}

// 验证 toString() 默认不显示微秒，并且复用 toFormattedString(false)。
TEST(TimestampTest, ToString)
{
    Timestamp ts(0);

    const std::string value = ts.toString();

    EXPECT_EQ(value, ts.toFormattedString(false));
    EXPECT_EQ(value.size(), 19u);
    EXPECT_EQ(value.find('.'), std::string::npos);
}

// 验证格式化输出默认不包含微秒。
TEST(TimestampTest, FormattedStringWithoutMicroseconds)
{
    Timestamp ts(123456);

    const std::string value = ts.toFormattedString(false);

    EXPECT_EQ(value.size(), 19u);
    EXPECT_EQ(value.find('.'), std::string::npos);
}

// 验证格式化输出能够保留 6 位微秒。
TEST(TimestampTest, FormattedStringWithMicroseconds)
{
    Timestamp ts(123456);

    const std::string value = ts.toFormattedString(true);

    EXPECT_EQ(value.size(), 26u);
    EXPECT_EQ(value.substr(19), ".123456");
}

// 验证整秒时间戳的微秒部分格式为 000000。
TEST(TimestampTest, FormattedStringAtSecondBoundary)
{
    Timestamp ts(1000000);

    EXPECT_EQ(ts.toFormattedString(true).substr(19), ".000000");
}

// 验证负时间戳的微秒部分仍然保持在 [0, 1 秒) 范围内。
TEST(TimestampTest, FormattedStringBeforeEpoch)
{
#if defined(D_OS_WIN32)
    GTEST_SKIP() << "MSVC localtime_s rejects negative time_t values.";
#else
    Timestamp ts(-1);

    EXPECT_EQ(ts.toFormattedString(true).substr(19), ".999999");
#endif
}

// 验证 AddTime 能够增加整数和小数秒。
TEST(TimestampTest, AddTime)
{
    Timestamp timestamp(1000000);

    EXPECT_EQ(Timestamp::AddTime(timestamp, 2.5), Timestamp(3500000));
    EXPECT_EQ(Timestamp::AddTime(timestamp, -0.25), Timestamp(750000));
}

// 验证相等和小于比较运算符。
TEST(TimestampTest, Comparison)
{
    Timestamp first(100);
    Timestamp same(100);
    Timestamp later(200);

    EXPECT_TRUE(first == same);
    EXPECT_FALSE(first == later);
    EXPECT_TRUE(first < later);
    EXPECT_FALSE(later < first);
    EXPECT_FALSE(first < same);
}

// 验证后一次获取的时间戳会大于前一次。
TEST(TimestampTest, Increasing)
{
    Timestamp t1 = Timestamp::Now();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    Timestamp t2 = Timestamp::Now();

    // 后获取的时间应该更大。
    EXPECT_GT(t2.microSecondsSinceEpoch(), t1.microSecondsSinceEpoch());
}
