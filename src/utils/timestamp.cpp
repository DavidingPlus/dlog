#include "timestamp.h"

#include <chrono>
#include <ctime>

#include <fmt/chrono.h>


Timestamp Timestamp::Now()
{
    // 获取当前系统时间。
    // 注意，因为是要获取绝对时间，因此不能使用只单调递增的 steady_clock，因为系统的绝对时间可能改变。
    auto now = std::chrono::system_clock::now();
    // 获取从 Unix epoch (1970-01-01 00:00:00 UTC) 到当前时间经过的微秒数。
    auto microSeconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();


    return Timestamp(microSeconds);
}

std::string Timestamp::toString() const
{
    // 将保存的微秒时间戳转换成 system_clock::time_point。
    auto tp = std::chrono::system_clock::time_point(std::chrono::microseconds(m_microSecondsSinceEpoch));
    // 先截断到整秒，再单独计算子秒部分，最后只把字符串格式化交给 fmt。
    auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(tp);

    // 对负时间戳，time_point_cast 会向 0 方向截断。例如 tp = -0.2s 时，截断后的 seconds 会先变成 0s，导致后面的子秒部分变成 -0.2s。这里把整秒部分回退到 -1s，最终就能拆成 (-1s) + 0.8s，保证纳秒部分始终落在 [0, 1s)。
    if (seconds > tp) seconds -= std::chrono::seconds(1);

    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(tp - seconds).count();
    auto time = std::chrono::system_clock::to_time_t(seconds);

    // 本项目只考虑 Linux 操作系统。
    // #if defined(_WIN32)
    //     localtime_s(&localTime, &time);
    // #else
    //     localtime_r(&time, &localTime);
    // #endif
    // 避免使用返回静态缓冲区的 std::localtime，减少线程间相互覆盖的风险。
    std::tm localTime{};
    localtime_r(&time, &localTime);


    return fmt::format("{:%Y/%m/%d %H:%M:%S}.{:09}", localTime, nanoseconds);
}
