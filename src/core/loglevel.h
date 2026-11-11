#ifndef _DLOG_LOGLEVEL_H_
#define _DLOG_LOGLEVEL_H_

#include "globalmacros.h"


DLOG_NAMESPACE_BEGIN(dlog)


// 日志等级。
enum class LogLevel
{
    TRACE,       // 最详细的跟踪信息。
    DEBUG,       // 调试信息。
    INFO,        // 普通运行信息。
    WARN,        // 警告，但程序通常还能继续运行。
    ERROR,       // 错误，需要处理。
    FATAL,       // 最高严重级别，当前实现不终止程序。
    LEVEL_COUNT, // 等级数量，不是真正的日志等级。
};


// 日志等级文字的颜色模式。
enum class LogLevelColorMode
{
    ON,  // 输出带颜色的日志等级。
    OFF, // 输出纯文本日志等级。
};


DLOG_NAMESPACE_END


#endif
