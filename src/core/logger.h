#ifndef _DLOG_LOGGER_H_
#define _DLOG_LOGGER_H_

#include "logmessage.h"


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


// 当前兼容入口仍表示一条临时日志，后续改掉；记录格式化与析构提交委托给内部 LogMessage。
// DLOG_INFO() << value 在完整表达式结束时提交；命名 Logger 在离开作用域时提交。
// LogStream 只负责拼接，输出按显式字节长度处理，保留空字节和超长正文。
// FATAL 在提交后调用记录创建时绑定的刷新回调，再 abort；异步排空留待后续步骤接入。
class DLOG_API_EXPORTED Logger
{

    DLOG_CLASS_NONCOPYABLE(Logger)

public:

    using OutputFunc = LogMessage::OutputFunc;

    using FlushFunc = LogMessage::FlushFunc;

    // savedErrno 为调用方保存的 errno；0 表示不附加错误信息。
    Logger(const char *filename, int line, LogLevel level, int savedErrno = 0);

    ~Logger() = default;

    LogStream &stream() { return m_message.stream(); }

    // 开始产生日志前设置，禁止并发修改；已创建的记录保留原回调和颜色。
    // 默认启用等级颜色，写文件时传入 LogLevelColorMode::OFF。
    static void SetOutput(OutputFunc, LogLevelColorMode colorMode = LogLevelColorMode::ON);

    static void SetFlush(FlushFunc);


private:

    LogMessage m_message;
};


DLOG_NAMESPACE_END


#endif
