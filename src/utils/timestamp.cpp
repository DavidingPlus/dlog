#include "timestamp.h"

#include "globalmacros.h"

#include <chrono>
#include <ctime>
#include <stdexcept>

#include <fmt/chrono.h>


const int Timestamp::kMicroSecondsPerSecond = 1000 * 1000;


std::string Timestamp::toFormattedString(bool showMicroseconds) const
{
    // 将保存的微秒时间戳转换成 system_clock::time_point。
    auto tp = std::chrono::system_clock::time_point(std::chrono::microseconds(m_microSecondsSinceEpoch));
    // 先截断到整秒，再单独计算子秒部分，最后只把字符串格式化交给 fmt。
    auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(tp);

    // 对负时间戳，time_point_cast 会向 0 方向截断。例如 tp = -0.2s 时，截断后的 seconds 会先变成 0s，导致后面的子秒部分变成 -0.2s。这里把整秒部分回退到 -1s，最终就能拆成 (-1s) + 0.8s，保证纳秒部分始终落在 [0, 1s)。
    if (seconds > tp) seconds -= std::chrono::seconds(1);

    auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(tp - seconds).count();
    auto time = std::chrono::system_clock::to_time_t(seconds);

    // 避免使用返回静态缓冲区的 std::localtime，减少线程间相互覆盖的风险。
    std::tm localTime{};

#if defined(D_OS_WIN32)
    localtime_s(&localTime, &time);
#elif defined(D_OS_LINUX)
    localtime_r(&time, &localTime);
#else
    throw std::runtime_error("Unsupported Operating System");
#endif


    return showMicroseconds
               ? fmt::format("{:%Y/%m/%d %H:%M:%S}.{:06}", localTime, microseconds)
               : fmt::format("{:%Y/%m/%d %H:%M:%S}", localTime);
}

Timestamp Timestamp::Now()
{
    // 获取当前系统时间。
    // 注意，因为是要获取绝对时间，因此不能使用只单调递增的 steady_clock，因为系统的绝对时间可能改变。
    auto now = std::chrono::system_clock::now();
    // 获取从 Unix epoch (1970-01-01 00:00:00 UTC) 到当前时间经过的微秒数。
    auto microSeconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();


    return Timestamp(microSeconds);
}

Timestamp Timestamp::AddTime(const Timestamp &timestamp, double seconds)
{
    // 将延时的秒数转换为微妙。
    int64_t delta = static_cast<int64_t>(seconds * Timestamp::kMicroSecondsPerSecond);
    // 返回新增时后的时间戳。
    return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}
