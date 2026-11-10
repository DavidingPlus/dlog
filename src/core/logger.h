#ifndef _DLOG_LOGGER_H_
#define _DLOG_LOGGER_H_

#include "logmessage.h"
#include "timestamp.h"

#include <string_view>
#include <functional>


// 日志宏采用类似 Qt qDebug() 的函数式调用方式：DLOG_INFO() << "server started" << port; 宏展开为临时 Logger 的 LogStream 引用，并捕获宏调用处的文件名和行号，当前完整表达式结束后，临时 Logger 析构并输出整条日志。
// 系统错误日志由调用方传入已经保存的 errno；应在确认系统调用失败后立即保存，避免后续调用改写 errno。示例：const int savedErrno = errno; DLOG_SYS_ERROR(savedErrno) << "open file failed";
#define DLOG_TRACE() (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::TRACE).stream())
#define DLOG_DEBUG() (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::DEBUG).stream())
#define DLOG_INFO() (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::INFO).stream())
#define DLOG_WARN() (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::WARN).stream())
#define DLOG_ERROR() (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::ERROR).stream())
#define DLOG_FATAL() (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::FATAL).stream())
#define DLOG_SYS_ERROR(savedErrno) (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::ERROR, (savedErrno)).stream())
#define DLOG_SYS_FATAL(savedErrno) (::dlog::Logger(__FILE__, __LINE__, ::dlog::LogLevel::FATAL, (savedErrno)).stream())


DLOG_NAMESPACE_BEGIN(dlog)


// Logger 负责一条日志消息的生命周期管理和元数据拼接，LogStream 负责具体的格式化与缓冲。
// 一条日志的典型执行流程是：
// 1. 创建 Logger 时，LoggerImpl 将时间、日志等级和源文件位置写入内部 LogStream 的前缀；
// 2. 调用 stream() 获取 LogStream，通过重载的 operator<< 将正文格式化后追加到可增长缓冲区；
// 3. Logger 析构时调用 finish()，为日志追加换行符；
// 4. 析构函数通过 OutputFunc 将缓冲区中的有效字节写到 stdout 或调用方指定的输出位置。
// Logger 本身不负责打开或管理日志文件。默认输出回调写入 stdout；如果调用 SetOutput() 注册文件输出回调，则可以将同一条日志交给其他文件后端持久化。颜色由 SetOutput() 的 LogLevelColorMode 参数控制，默认 ON；文件后端需要纯文本时应传入 LogLevelColorMode::OFF。OutputFunc 接收 data 和 length 两个参数，因此缓冲区是“起始地址 + 有效长度”的字节序列，不保证以 '\0' 结尾，不能直接按 C 字符串处理。
// 典型的临时对象用法如下：Logger(__FILE__, __LINE__, LogLevel::INFO).stream() << "server started"; 当前完整表达式结束后，临时 Logger 析构并输出整条日志。若先保存为命名对象，则会在对象离开作用域时输出。FATAL 日志在输出后还会调用 FlushFunc 刷新输出，并终止进程；因此不应在普通单元测试中直接触发 FATAL。
class DLOG_API_EXPORTED Logger
{

    DLOG_CLASS_NONCOPYABLE(Logger)

public:

    // savedErrno 是调用方在系统错误发生时保存的 errno 快照；0 表示不附带系统错误信息。
    Logger(const char *filename, int line, LogLevel level, int savedErrno = 0) : m_impl(level, savedErrno, filename, line) {}

    ~Logger();

    // 返回当前日志的内部流。后续的 operator<< 只会修改本条日志自己的可增长缓冲区，真正输出发生在 Logger 析构时。
    LogStream &stream() { return m_impl.m_stream; }

    // 输出函数。OutputFunc 使用 size_t 表示显式长度，因此调用方不需要、也不应该依赖 data 以 '\0' 结尾。
    using OutputFunc = std::function<void(const char *msg, size_t len)>;

    // 刷新缓冲区的函数。
    using FlushFunc = std::function<void()>;

    // 设置进程内共享的输出回调和颜色模式。默认启用颜色；应在开始产生日志前完成设置，避免运行期间并发修改配置，或在 Logger 对象存活期间切换配置。
    static void SetOutput(OutputFunc, LogLevelColorMode colorMode = LogLevelColorMode::ON);

    static void SetFlush(FlushFunc);


private:

    class LoggerImpl
    {

    public:

        LoggerImpl(LogLevel level, int savedErrno, const char *filename, int line);

        // 格式化一条 log 的时间部分。formatTime() 只修改 m_stream，不负责把缓冲区写入终端或日志文件。
        void formatTime();

        // 完成一条 log 消息：在已格式化的前缀和用户正文后追加换行符。同 formatTime()，也只修改 m_stream。
        void finish() { m_stream << '\n'; }


        // 日志创建时的时间戳。
        Timestamp m_time;

        // 日志的正文缓冲区。
        LogStream m_stream;

        // 日志的严重程度。
        LogLevel m_level;

        // 日志的源文件名，通常由 __FILE__ 宏传入。
        FileNameView m_basename;

        // 产生日志的源代码行号，通常由 __LINE__ 宏传入。它和 m_basename 组成调用位置，例如："[logger.cpp:42]"，方便定位日志是从哪里产生的。
        int m_line;
    };


    LoggerImpl m_impl;
};


DLOG_NAMESPACE_END


#endif
