#include "logmanager.h"

#include <exception>
#include <stdexcept>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


LogManager::~LogManager()
{
    // 保存第一个停止异常，不要遇到异常就停止程序，让其余日志器仍有机会完成排空。全部停止后重新抛出，析构函数会因此终止进程。
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

void LogManager::registerLogger(std::string name, Logger::Ptr logger)
{
    if (name.empty() || !logger) throw std::invalid_argument("LogManager::registerLogger(): name and logger must not be empty");

    std::unique_lock<std::shared_mutex> lock(m_loggerMapMutex);

    if (!m_loggers.try_emplace(std::move(name), std::move(logger)).second) throw std::logic_error("LogManager::registerLogger(): logger name already registered");
}

Logger::Ptr LogManager::getLogger(const std::string &name) const
{
    std::shared_lock<std::shared_mutex> lock(m_loggerMapMutex);

    auto found = m_loggers.find(name);
    return m_loggers.end() != found ? found->second : nullptr;
}


DLOG_NAMESPACE_END
