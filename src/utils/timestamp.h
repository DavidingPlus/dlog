#ifndef _DLOG_TIMESTAMP_H_
#define _DLOG_TIMESTAMP_H_

#include "globalmacros.h"

#include <string>


class D_API_EXPORTED Timestamp
{

public:

    Timestamp() = default;

    explicit Timestamp(int64_t microSecondsSinceEpoch) : m_microSecondsSinceEpoch(microSecondsSinceEpoch) {}

    ~Timestamp() = default;

    int64_t microSecondsSinceEpoch() const { return m_microSecondsSinceEpoch; }

    // 返回当前时间戳对应的秒数。
    time_t secondsSinceEpoch() const { return static_cast<time_t>(m_microSecondsSinceEpoch / kMicroSecondsPerSecond); }

    // 按指定格式输出时间戳，不显示微秒。
    std::string toString() const { return toFormattedString(false); }

    // 按指定格式输出时间戳，可选择是否显示微秒。
    std::string toFormattedString(bool showMicroseconds = false) const;


    // 获取当前系统时间戳。
    static Timestamp Now();

    // 返回无效时间戳。
    static Timestamp Invalid() { return Timestamp(); }

    // 返回增加指定秒数后的时间戳。
    static Timestamp AddTime(const Timestamp &timestamp, double seconds);


    friend bool operator==(const Timestamp &lhs, const Timestamp &rhs) { return lhs.m_microSecondsSinceEpoch == rhs.m_microSecondsSinceEpoch; }

    friend bool operator<(const Timestamp &lhs, const Timestamp &rhs) { return lhs.m_microSecondsSinceEpoch < rhs.m_microSecondsSinceEpoch; }


    // 1 秒 = 1000 * 1000 微秒。
    static const int kMicroSecondsPerSecond;


private:

    int64_t m_microSecondsSinceEpoch = static_cast<int64_t>(0);
};


#endif
