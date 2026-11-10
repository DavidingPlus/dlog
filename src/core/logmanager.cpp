#include "logmanager.h"

#include <exception>
#include <stdexcept>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


LogManager &LogManager::GetInstance()
{
    static LogManager manager;

    // 局部静态变量只初始化一次，避免调用 GetInstance() 时，重复注册退出回调。
    static bool registration = std::atexit([]
                                           {
                                               try
                                               {
                                                   manager.shutdown();
                                               }
                                               catch (...)
                                               {
                                                   std::fputs("dlog: LogManager shutdown failed\n", stderr);
                                               } //
                                           });
    if (registration) throw std::runtime_error("LogManager: could not register application exit handler");


    return manager;
}

void LogManager::registerLogger(std::string name, AsyncLogger::Ptr logger)
{
    if (name.empty() || !logger) throw std::invalid_argument("LogManager::registerLogger(): name and logger must not be empty");

    std::unique_lock<std::shared_mutex> lock(m_mutex);

    if (!m_loggers.try_emplace(std::move(name), std::move(logger)).second) throw std::logic_error("LogManager::registerLogger(): logger name already registered");
}

AsyncLogger::Ptr LogManager::getLogger(const std::string &name) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    auto found = m_loggers.find(name);
    return m_loggers.end() != found ? found->second : nullptr;
}

void LogManager::shutdown()
{
    // 退出前须结束生产者及注册操作，关闭期间不得修改命名表。输出回调可以查询管理器，等待 Worker 时不能持有管理锁。
    std::exception_ptr firstError;

    auto stop = [&firstError](const AsyncLogger::Ptr &logger)
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


DLOG_NAMESPACE_END
