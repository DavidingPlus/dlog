#include "asynclogger.h"


DLOG_NAMESPACE_BEGIN(dlog)


AsyncLogger::AsyncLogger(std::shared_ptr<LogFlush> output)
    : m_worker([output = std::move(output)](const LargeFixedBuffer &buffer)
               { output->writeAndFlush(buffer.readableBegin(), buffer.readableBytes()); }) {}


std::shared_ptr<AsyncLogger> AsyncLoggerBuilder::Build()
{
    auto output = m_output ? m_output : LogFlushFactory::CreateLogFlush<StdoutFlush>();
    // 私有构造不能经 make_shared 调用；先建立所有权，启动失败时也由析构完成回收。
    auto logger = std::shared_ptr<AsyncLogger>(new AsyncLogger(std::move(output)));

    logger->m_worker.start();


    return logger;
}


DLOG_NAMESPACE_END
