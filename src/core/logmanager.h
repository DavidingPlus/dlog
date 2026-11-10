#ifndef _DLOG_LOGMANAGER_H_
#define _DLOG_LOGMANAGER_H_

#include "asynclogger.h"

#include <mutex>
#include <string>
#include <unordered_map>


DLOG_NAMESPACE_BEGIN(dlog)


class DLOG_API_EXPORTED LogManager
{

    DLOG_CLASS_NONCOPYABLE(LogManager)

public:

    static LogManager &GetInstance();


    // 空名称或空目标抛出 invalid_argument，重复名称抛出 logic_error。不推荐用多个名称注册同一实例或注册默认实例，关闭时会重复调用 stop()，后果不可预期。
    void registerLogger(std::string name, AsyncLogger::Ptr logger);

    AsyncLogger::Ptr getLogger(const std::string &name) const;

    AsyncLogger::Ptr getDefaultLogger() const { return m_defaultLogger; }


private:

    LogManager() : m_defaultLogger(AsyncLogger::BuildAsyncLogger()) {}

    ~LogManager() = default;

    void shutdown();


    mutable std::mutex m_mutex;

    // 固定的默认终端日志器，独立于命名表。
    AsyncLogger::Ptr m_defaultLogger;

    // 注册进命名表的其他日志器。
    std::unordered_map<std::string, AsyncLogger::Ptr> m_loggers;
};


DLOG_NAMESPACE_END


#endif
