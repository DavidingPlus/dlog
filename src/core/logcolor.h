#ifndef _DLOG_LOGCOLOR_H_
#define _DLOG_LOGCOLOR_H_

#include "globalmacros.h"

class LogStream;
enum class LogLevel;


// 重置为默认颜色。
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


// 以 RAII 方式在 LogStream 中标记一段带颜色的内容。构造时向指定流写入等级对应的 ANSI 颜色码；调用方在 guard 存活期间继续向同一流写入日志文字；析构时再向该流写入重置码。用法类似 std::lock_guard：对象的作用域决定颜色标记的范围，正常离开作用域或异常展开时都会执行析构重置。
// LogColorGuard 只写入颜色控制码，不负责加锁或线程同步；它保存的 LogStream 必须比 guard 活得更久。
class D_API_EXPORTED LogColorGuard
{

    D_CLASS_NONCOPYABLE(LogColorGuard)

public:

    // 构造时立即向 stream 写入 level 对应的颜色码；后续文字需由调用方写入同一个 stream。
    explicit LogColorGuard(LogStream &stream, LogLevel level);

    // 析构时向同一个 stream 写入重置码，使终端回到默认颜色；不会保存或恢复 guard 构造前的其他颜色状态。
    ~LogColorGuard();


private:

    // 非拥有引用，指向构造函数传入的目标流。构造和析构都向这同一个流追加控制码；调用方必须保证该流在 guard 销毁前仍然存活。
    LogStream &m_stream;
};


#endif
