#include "logmanager.h"

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <unordered_set>
#include <utility>

DLOG_NAMESPACE_BEGIN(dlog)


LogManager &LogManager::GetInstanceImpl(int (*registerAtExit)(void (*)()))
{
    static LogManager manager;
    static const bool registered = registerAtExit([]
                                                  {
        try
        {
            manager.Shutdown();
        }
        catch (...)
        {
            std::fputs("dlog: LogManager shutdown failed\n", stderr);
        } }) == 0;
    if (!registered) throw std::runtime_error("LogManager: could not register application exit handler");
    return manager;
}

void LogManager::RegisterLogger(std::string name, AsyncLogger::Ptr logger)
{
    if (name.empty() || !logger) throw std::invalid_argument("LogManager::RegisterLogger(): name and logger must not be empty");

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_defaultLogger) throw std::logic_error("LogManager: already shut down");

    if (!m_loggers.try_emplace(std::move(name), std::move(logger)).second) throw std::logic_error("LogManager::RegisterLogger(): logger name already registered");
}

AsyncLogger::Ptr LogManager::GetLogger(const std::string &name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_loggers.find(name);
    return m_loggers.end() != found ? found->second : nullptr;
}

void LogManager::SetDefaultLogger(AsyncLogger::Ptr logger)
{
    if (!logger) throw std::invalid_argument("LogManager::SetDefaultLogger(): logger must not be null");

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_defaultLogger) throw std::logic_error("LogManager: already shut down");
    // 用参数带走旧目标，在释放管理锁后才可能析构并等待它的 Worker。
    m_defaultLogger.swap(logger);
}

AsyncLogger::Ptr LogManager::DefaultLogger() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_defaultLogger;
}

void LogManager::Shutdown()
{
    std::unordered_set<AsyncLogger::Ptr> loggers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_defaultLogger) return;

        loggers.insert(m_defaultLogger);
        for (const auto &entry : m_loggers) loggers.insert(entry.second);
        m_loggers.clear();
        m_defaultLogger.reset();
    }

    // 输出回调可以查询管理器，等待 Worker 时不能持有管理锁。
    std::exception_ptr firstError;
    for (const auto &logger : loggers)
    {
        try
        {
            logger->stop();
        }
        catch (...)
        {
            if (!firstError) firstError = std::current_exception();
        }
    }
    if (firstError) std::rethrow_exception(firstError);
}


DLOG_NAMESPACE_END
