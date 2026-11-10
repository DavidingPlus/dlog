#include "asynclogger.h"

#include <stdexcept>


DLOG_NAMESPACE_BEGIN(dlog)


// 先持有已构造的输出目标，再为 Worker 绑定批次写入回调，此时线程尚未启动。Worker 先于 m_output 析构并排空，因此回调只需保存指向输出目标的指针。
AsyncLogger::AsyncLogger(std::shared_ptr<LogFlush> output)
    : m_output(std::move(output)),
      m_worker([output = m_output.get()](const LargeFixedBuffer &buffer)
               { output->writeAndFlush(buffer.readableBegin(), buffer.readableBytes()); }) {}

LogMessage AsyncLogger::message(const char *filename, int line, LogLevel level, int savedErrno)
{
    if (level < LogLevel::TRACE || level > LogLevel::ERROR) throw std::invalid_argument("AsyncLogger::message(): unsupported log level");
    if (m_stopped) throw std::logic_error("AsyncLogger: logger has stopped");

    return LogMessage(
        filename, line, level, savedErrno,
        // shared_from_this() 取得与 Build() 返回值共享所有权的 shared_ptr。即使调用方先释放自己的引用，消息仍持有日志器和 Worker，直到析构回调完成入队。只捕获 this 时，日志器可能已被销毁，析构提交会访问悬空指针。
        [logger = shared_from_this()](const char *data, size_t len)
        {
            if (logger->m_stopped) throw std::logic_error("AsyncLogger: logger has stopped");

            logger->m_worker.append(data, len);
        },
        [] {}, m_output->colorMode());
}

void AsyncLogger::stop()
{
    m_stopped = true;
    m_worker.stop();
}

std::shared_ptr<AsyncLogger> AsyncLogger::BuildWithOutput(std::shared_ptr<LogFlush> output)
{
    // 先建立 shared_ptr 所有权，供后续 message() 的 shared_from_this() 使用。Worker 启动成功后返回日志器。启动抛异常时，局部 shared_ptr 负责回收已构造的资源。
    auto logger = std::shared_ptr<AsyncLogger>(new AsyncLogger(std::move(output)));
    logger->m_worker.start();
    return logger;
}


DLOG_NAMESPACE_END
