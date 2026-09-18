#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "timestamp.h"


// 验证默认构造得到的时间戳是 0。
TEST(TimestampTest, DefaultConstructor)
{
    Timestamp ts;

    // 默认构造时间戳应该为 0。
    EXPECT_EQ(ts.microSecondsSinceEpoch(), 0);
}

// 验证 now() 返回的时间接近系统当前时间。
TEST(TimestampTest, Now)
{
    Timestamp ts = Timestamp::Now();

    // 获取当前系统时间。
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // Timestamp::Now() 获取的时间应该接近当前时间。由于程序执行存在耗时，所以允许 1 秒误差。
    EXPECT_NEAR(ts.microSecondsSinceEpoch(), now, 1000000);
}

// 验证 toString() 的格式输出，当前只人工打印便于观察。
TEST(TimestampTest, ToString)
{
    Timestamp ts(0);

    // Unix epoch：1970/01/01 08:00:00.000000000（北京时间）。
    // 因为 GitHub CI 用的机器的时区可能不一样，暂时注释掉防止影响 CI 运行。
    // EXPECT_EQ(ts.toString(), "1970/01/01 08:00:00.000000000");

    std::cout << ts.toString() << std::endl;
}

// 验证微秒值会保留在时间戳对象里并出现在字符串中。
TEST(TimestampTest, MicroSeconds)
{
    Timestamp ts(123456);

    EXPECT_EQ(ts.microSecondsSinceEpoch(), 123456);

    // 微秒部分应该保留。
    EXPECT_TRUE(std::string::npos != ts.toString().find(".123456"));
}

// 验证后一次 now() 的时间戳会大于前一次。
TEST(TimestampTest, Increasing)
{
    Timestamp t1 = Timestamp::Now();

    // 等待一点时间。
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    Timestamp t2 = Timestamp::Now();

    // 后获取的时间应该更大。
    EXPECT_GT(t2.microSecondsSinceEpoch(), t1.microSecondsSinceEpoch());
}
