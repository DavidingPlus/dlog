#include "logmanager.h"


DLOG_NAMESPACE_BEGIN(dlog)


Logger::SharedPtr LogManager::buildLoggerImpl(std::shared_ptr<LogFlush> output)
{
    // 建立 shared_ptr 所有权，供 Logger::message() 的 shared_from_this() 使用。Worker 启动成功后返回日志器，抛异常时，局部 shared_ptr 负责回收已构造的资源。
    auto logger = Logger::SharedPtr(new Logger(std::move(output), m_threadPool));
    logger->m_worker.start();
    return logger;
}

void LogManager::shutdown()
{
    // 关闭前，调用方需要停止外部日志生产者和线程池任务提交者，并结束临时日志消息。为了保证销毁的时序正确，具体分为三个阶段：
    // 1. 等待线程池空闲：已有任务可能继续写日志或提交后续任务，因此等待期间线程池仍需接收任务，直到任务队列为空且所有正在执行的任务（包括日志输出任务）都已完成。
    // 2. 停止所有受管理的 Logger：等待各 Worker 的输出任务结束，并记录后台输出异常；即使某个 Logger 停止时发生异常，也继续停止其余 Logger。
    // 3. 停止并回收线程池：所有 Logger 停止后关闭线程池、等待工作线程退出；若前一阶段记录了异常，在线程池回收后重新抛出第一个异常。

    {
        std::unique_lock<std::shared_mutex> lock(m_loggerMapMutex);
        if (m_shutdown) return;
        m_shutdown = true;
    }

    // 已有任务可能继续写日志；等待全部任务完成时，线程池仍需接收日志输出任务。
    m_threadPool->waitUntilIdle();

    // 保存第一个停止异常，让其余日志器仍有机会完成排空；全部停止后再抛出异常。
    std::exception_ptr firstError;

    auto stop = [&firstError](const Logger::SharedPtr &logger)
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

    // 所有日志器均已停止，再关闭线程池，即使停止时有异常也要回收。
    m_threadPool->stopAndJoin();

    if (firstError) std::rethrow_exception(firstError);
}

Logger::SharedPtr LogManager::getLogger(const std::string &name) const
{
    std::shared_lock<std::shared_mutex> lock(m_loggerMapMutex);

    auto found = m_loggers.find(name);
    return m_loggers.end() != found ? found->second : nullptr;
}


DLOG_NAMESPACE_END
