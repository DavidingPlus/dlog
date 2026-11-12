#include "logmanager.h"


DLOG_NAMESPACE_BEGIN(dlog)


Logger::Ptr LogManager::buildLoggerImpl(std::shared_ptr<LogFlush> output)
{
    // 建立 shared_ptr 所有权，供 Logger::message() 的 shared_from_this() 使用。Worker 启动成功后返回日志器，抛异常时，局部 shared_ptr 负责回收已构造的资源。
    auto logger = Logger::Ptr(new Logger(std::move(output), m_threadPool));
    logger->m_worker.start();
    return logger;
}

void LogManager::shutdown()
{
    {
        std::unique_lock<std::shared_mutex> lock(m_loggerMapMutex);
        if (m_shutdown) return;
        m_shutdown = true;
    }

    // 任务可能写日志，先等待线程池排空，再停止日志器。
    m_threadPool->stopAndJoin();

    // 保存第一个停止异常，让其余日志器仍有机会完成排空；全部停止后再抛出异常。
    std::exception_ptr firstError;

    auto stop = [&firstError](const Logger::Ptr &logger)
    {
        try
        {
            logger->stop();
        }
        catch (...)
        {
            if (!firstError) firstError = std::current_exception();
        }
    };

    stop(m_defaultLogger);
    for (auto &entry : m_loggers) stop(entry.second);

    if (firstError) std::rethrow_exception(firstError);
}

Logger::Ptr LogManager::getLogger(const std::string &name) const
{
    std::shared_lock<std::shared_mutex> lock(m_loggerMapMutex);

    auto found = m_loggers.find(name);
    return m_loggers.end() != found ? found->second : nullptr;
}


DLOG_NAMESPACE_END
