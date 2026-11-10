#ifndef _DLOG_ASYNCLOGGER_H_
#define _DLOG_ASYNCLOGGER_H_

#include "asyncworker.h"
#include "logflush.h"
#include "logmessage.h"

#include <atomic>
#include <memory>
#include <stdexcept>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


// 长期日志器，由 AsyncLoggerBuilder 创建并启动；最后一个 shared_ptr 释放时自动回收 Worker。
class DLOG_API_EXPORTED AsyncLogger : public std::enable_shared_from_this<AsyncLogger>
{

    DLOG_CLASS_NONCOPYABLE(AsyncLogger)

    friend class AsyncLoggerBuilder;

public:

    // Worker 先停止并 join，再释放其回调持有的输出对象；析构不重抛后台错误。
    ~AsyncLogger() = default;

    // 消息持有日志器，析构时提交。当前仅接受 TRACE 至 ERROR。
    LogMessage message(const char *filename, int line, LogLevel level, int savedErrno = 0);

    // 可选的主动等待：正常停止会排空，后台失败在回收线程后重抛；可重复调用，不提供重启。
    // 调用方必须先结束生产者与临时消息，再串行调用 stop；不得从输出回调中停止或销毁本对象。
    void stop();


private:


    explicit AsyncLogger(std::shared_ptr<LogFlush> output);


    LogLevelColorMode m_colorMode;

    std::atomic_bool m_stopped{false};

    AsyncWorker m_worker;
};


// 配置时就可能创建目录或打开文件；Build 返回前启动 Worker，之后 Builder 不参与写入或停止。
class DLOG_API_EXPORTED AsyncLoggerBuilder
{

    DLOG_CLASS_NONCOPYABLE(AsyncLoggerBuilder)

public:

    template <typename LogFlushType, typename... Args>
    void BuildLoggerFlush(Args &&...args);

    // 默认使用 StdoutFlush。成功构建后，Builder 不再接受配置或构建。
    std::shared_ptr<AsyncLogger> Build();


private:

    std::shared_ptr<LogFlush> m_output;

    bool m_built = false;
};


template <typename LogFlushType, typename... Args>
void AsyncLoggerBuilder::BuildLoggerFlush(Args &&...args)
{
    if (m_built) throw std::logic_error("AsyncLoggerBuilder::BuildLoggerFlush(): builder already used");

    // TODO 当前仅支持单输出，步骤 6 扩展多目标时调整此限制。
    if (m_output) throw std::logic_error("AsyncLoggerBuilder::BuildLoggerFlush(): only one output is supported");

    // 右侧构造成功后才赋值，失败时仍可重新配置。
    m_output = LogFlushFactory::CreateLogFlush<LogFlushType>(std::forward<Args>(args)...);
}


DLOG_NAMESPACE_END


#endif
