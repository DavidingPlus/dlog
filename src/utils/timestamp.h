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

    // 获取当前系统时间戳。
    static Timestamp Now();

    std::string toString() const;


private:

    int64_t m_microSecondsSinceEpoch = static_cast<int64_t>(0);
};


#endif
