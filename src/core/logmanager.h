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

    // 首次从主程序调用时注册进程退出回调；插件独立卸载留待需要时处理。
    static LogManager &GetInstance()
    {
        return GetInstanceImpl([](void (*handler)())
                               { return std::atexit(handler); });
    }

    // 空名称或空目标抛出 invalid_argument，重复名称抛出 logic_error。
    void RegisterLogger(std::string name, AsyncLogger::Ptr logger);

    AsyncLogger::Ptr GetLogger(const std::string &name) const;

    void SetDefaultLogger(AsyncLogger::Ptr logger);

    AsyncLogger::Ptr DefaultLogger() const;


private:

    LogManager() : m_defaultLogger(AsyncLogger::BuildAsyncLogger()) {}

    ~LogManager() = default;

    static LogManager &GetInstanceImpl(int (*registerAtExit)(void (*)()));

    void Shutdown();


    mutable std::mutex m_mutex;

    AsyncLogger::Ptr m_defaultLogger;

    std::unordered_map<std::string, AsyncLogger::Ptr> m_loggers;
};


DLOG_NAMESPACE_END


#endif
