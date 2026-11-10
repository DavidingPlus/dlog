#include "asynclogger.h"


DLOG_NAMESPACE_BEGIN(dlog)


AsyncLogger::AsyncLogger(std::shared_ptr<LogFlush> output)
    : m_colorMode(dynamic_cast<StdoutFlush *>(output.get()) ? LogLevelColorMode::ON : LogLevelColorMode::OFF),
      m_worker([output = std::move(output)](const LargeFixedBuffer &buffer)
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
        [] {}, m_colorMode);
}

void AsyncLogger::stop()
{
    m_stopped = true;
    m_worker.stop();
}

std::shared_ptr<AsyncLogger> AsyncLoggerBuilder::Build()
{
    if (m_built) throw std::logic_error("AsyncLoggerBuilder::Build(): builder already used");

    auto output = m_output ? m_output : LogFlushFactory::CreateLogFlush<StdoutFlush>();
    auto logger = std::shared_ptr<AsyncLogger>(new AsyncLogger(std::move(output)));
    logger->m_worker.start();

    // 启动成功后再交出配置，失败时仍可用原配置重试。
    m_output.reset();
    m_built = true;


    return logger;
}


DLOG_NAMESPACE_END
