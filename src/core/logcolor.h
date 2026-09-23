#ifndef _DLOG_LOGCOLOR_H_
#define _DLOG_LOGCOLOR_H_

#include "globalmacros.h"

class LogStream;
enum class LogLevel;


// 清除所有颜色，恢复到默认。
#define DLOG_COLOR_RESET "\033[0m"

// 跟踪信息，白色。
#define DLOG_COLOR_TRACE "\033[37m"

// 调试信息，青色。
#define DLOG_COLOR_DEBUG "\033[36m"

// 普通信息，绿色。
#define DLOG_COLOR_INFO "\033[32m"

// 警告信息，粗体黄色。
#define DLOG_COLOR_WARN "\033[33m\033[1m"

// 错误信息，粗体红色。
#define DLOG_COLOR_ERROR "\033[31m\033[1m"

// 致命错误，粗体红底。
#define DLOG_COLOR_FATAL "\033[1m\033[41m"


class D_API_EXPORTED LogColorGuard
{

    D_CLASS_NONCOPYABLE(LogColorGuard)

public:

    // 构造时应用新传入等级对应的颜色。
    explicit LogColorGuard(LogStream &stream, LogLevel level);

    // 析构时自动恢复原始颜色。
    ~LogColorGuard();


private:

    LogStream &m_stream;
};


#endif
