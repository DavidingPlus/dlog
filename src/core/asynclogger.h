#ifndef _DLOG_ASYNCLOGGER_H_
#define _DLOG_ASYNCLOGGER_H_

#include "asyncworker.h"
#include "logflush.h"
#include "logmessage.h"

#include <atomic>
#include <memory>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


// 长期日志器，Build() 创建并启动 Worker；最后一个 shared_ptr 释放时自动回收。
class DLOG_API_EXPORTED AsyncLogger : public std::enable_shared_from_this<AsyncLogger>
{

    DLOG_CLASS_NONCOPYABLE(AsyncLogger)

public:

    // Build 通过模板参数选择输出类型，LogFlushFactory 将实参原样转发给该类型的构造函数。
    // 1. Build() 使用默认的 StdoutFlush，无需构造参数。
    // 2. Build<FileFlush>(filename) 按完整文件名打开普通日志文件。
    // 3. Build<RollFileFlush>(basePath, rollSize) 按基础路径和大小阈值初始化轮转日志文件。
    // 输出构造成功后，三种调用都进入 BuildWithOutput，通过构造函数创建日志器 AsyncLogger 并启动 Worker。
    template <typename LogFlushType = StdoutFlush, typename... Args>
    static std::shared_ptr<AsyncLogger> Build(Args &&...args) { return BuildWithOutput(LogFlushFactory::CreateLogFlush<LogFlushType>(std::forward<Args>(args)...)); }


    ~AsyncLogger() = default;

    // 消息持有日志器，析构时提交。当前仅接受 TRACE 至 ERROR。
    LogMessage message(const char *filename, int line, LogLevel level, int savedErrno = 0);

    // 可选的主动等待：正常停止会排空，后台失败在回收线程后重抛；可重复调用，不提供重启。
    // 调用方必须先结束生产者与临时消息，再串行调用 stop；不得从输出回调中停止或销毁本对象。
    void stop();


private:

    static std::shared_ptr<AsyncLogger> BuildWithOutput(std::shared_ptr<LogFlush> output);


    explicit AsyncLogger(std::shared_ptr<LogFlush> output);


    std::shared_ptr<LogFlush> m_output;

    std::atomic_bool m_stopped{false};

    // Worker 析构时会排空队列，必须先于输出目标销毁。
    AsyncWorker m_worker;
};


DLOG_NAMESPACE_END


#endif
