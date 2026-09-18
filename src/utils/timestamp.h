#ifndef _DLOG_TIMESTAMP_H_
#define _DLOG_TIMESTAMP_H_

#include <string>


class Timestamp
{

public:

    Timestamp() = default;

    explicit Timestamp(int64_t microSecondsSinceEpoch) : m_microSecondsSinceEpoch(microSecondsSinceEpoch) {}

    ~Timestamp() = default;

    int64_t microSecondsSinceEpoch() const { return m_microSecondsSinceEpoch; }

    std::string toString() const;

    // 按指定格式输出时间戳，可选择是否显示微秒。
    std::string toFormattedString(bool showMicroseconds = false) const;

    // 返回当前时间戳对应的秒数。
    time_t secondsSinceEpoch() const;

    // 获取当前系统时间戳。
    static Timestamp Now();

    // 返回无效时间戳。
    static Timestamp invalid() { return Timestamp(); }

    // 返回增加指定秒数后的时间戳。
    static Timestamp addTime(const Timestamp &timestamp, double seconds);

    friend bool operator==(const Timestamp &lhs, const Timestamp &rhs);

    friend bool operator<(const Timestamp &lhs, const Timestamp &rhs);


    // 1 秒 = 1000 * 1000 微秒。
    static const int kMicroSecondsPerSecond;


private:

    int64_t m_microSecondsSinceEpoch = static_cast<int64_t>(0);
};


#endif
