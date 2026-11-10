#ifndef _DLOG_ASYNCLOGGER_H_
#define _DLOG_ASYNCLOGGER_H_

#include "asyncworker.h"
#include "logflush.h"

#include <memory>
#include <stdexcept>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


// 长期日志器，由 AsyncLoggerBuilder 创建并启动；最后一个 shared_ptr 释放时自动回收 Worker。
// 当前仅接入资源生命周期，消息入口在 2.3 接通；原 Logger 仍作为临时日志兼容入口。
class DLOG_API_EXPORTED AsyncLogger
{

    DLOG_CLASS_NONCOPYABLE(AsyncLogger)

    friend class AsyncLoggerBuilder;

public:

    // Worker 先停止并 join，再释放其回调持有的输出对象；析构不重抛后台错误。
    ~AsyncLogger() = default;

    // 可选的主动等待：正常停止会排空，后台失败在回收线程后重抛；可重复调用，不提供重启。
    // 调用方应先结束生产者与临时消息，再串行控制停止；不得从输出回调中停止或销毁本对象。
    void stop() { m_worker.stop(); }


private:


    explicit AsyncLogger(std::shared_ptr<LogFlush> output);


    AsyncWorker m_worker;
};


// 配置时就可能创建目录或打开文件；Build 返回前启动 Worker，之后 Builder 不参与写入或停止。
class DLOG_API_EXPORTED AsyncLoggerBuilder
{

public:

    template <typename LogFlushType, typename... Args>
    void BuildLoggerFlush(Args &&...args);

    // 未配置时仅为本次构建创建 StdoutFlush；多次 Build 各有一个 Worker，但显式配置的输出对象共享。
    std::shared_ptr<AsyncLogger> Build();


private:

    std::shared_ptr<LogFlush> m_output;
};


template <typename LogFlushType, typename... Args>
void AsyncLoggerBuilder::BuildLoggerFlush(Args &&...args)
{
    // TODO 当前仅支持单输出，步骤 6 扩展多目标时调整此限制。
    if (m_output) throw std::logic_error("AsyncLoggerBuilder::BuildLoggerFlush(): only one output is supported");

    // 右侧构造成功后才赋值，失败时仍可重新配置。
    m_output = LogFlushFactory::CreateLogFlush<LogFlushType>(std::forward<Args>(args)...);
}


DLOG_NAMESPACE_END


#endif
