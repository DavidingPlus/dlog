#ifndef _DLOG_LOGGER_H_
#define _DLOG_LOGGER_H_

#include "logworker.h"
#include "logflush.h"
#include "logmessage.h"

#include <atomic>
#include <memory>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


// 长期日志器，通过 BuildLogger() 创建并启动 Worker。最后一个 shared_ptr 释放时自动回收。
class DLOG_API_EXPORTED Logger : public std::enable_shared_from_this<Logger>
{

    DLOG_CLASS_NONCOPYABLE(Logger)

public:

    using Ptr = std::shared_ptr<Logger>;

    // BuildLogger 通过模板参数选择输出类型，LogFlushFactory 将实参原样转发给该类型的构造函数。
    // 1. BuildLogger() 使用默认的 StdoutFlush，无需构造参数。
    // 2. BuildLogger<FileFlush>(filename) 按完整文件名打开普通日志文件。
    // 3. BuildLogger<RollFileFlush>(basePath, rollSize) 按基础路径和大小阈值初始化轮转日志文件。
    // 输出构造成功后，三种调用都进入 BuildLoggerImpl，通过构造函数创建日志器 Logger 并启动 Worker。
    template <typename LogFlushType = StdoutFlush, typename... Args>
    static Ptr BuildLogger(Args &&...args) { return BuildLoggerImpl(LogFlushFactory::CreateLogFlush<LogFlushType>(std::forward<Args>(args)...)); }


    ~Logger() = default;

    // 消息持有日志器，析构时提交。接受 TRACE 至 FATAL。
    LogMessage message(const char *filename, int line, LogLevel level, int savedErrno = 0);

    // 可选的主动等待：正常停止会排空，后台失败在回收线程后重抛；可重复调用，不提供重启。
    // 调用方必须先结束生产者与临时消息，再串行调用 stop；不得从输出回调中停止或销毁本对象。
    void stop();


private:

    static Ptr BuildLoggerImpl(std::shared_ptr<LogFlush> output);


    explicit Logger(std::shared_ptr<LogFlush> output);


    // 日志器是否正在运行。
    std::atomic_bool m_running{false};

    std::shared_ptr<LogFlush> m_output;

    // Worker 析构时会排空队列，必须先于输出目标销毁。
    LogWorker m_worker;
};


DLOG_NAMESPACE_END


#endif
