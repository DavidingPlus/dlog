#ifndef _DLOG_LOGMANAGER_H_
#define _DLOG_LOGMANAGER_H_

#include "asynclogger.h"

#include <shared_mutex>
#include <string>
#include <unordered_map>


DLOG_NAMESPACE_BEGIN(dlog)


class LogManager
{

    DLOG_CLASS_NONCOPYABLE(LogManager)

public:

    static LogManager &GetInstance();


    // 空名称或空目标抛出 invalid_argument，重复名称抛出 logic_error。不推荐用多个名称注册同一实例或注册默认实例，关闭时会重复调用 stop()，后果不可预期。
    DLOG_API_EXPORTED void registerLogger(std::string name, AsyncLogger::Ptr logger);

    DLOG_API_EXPORTED AsyncLogger::Ptr getLogger(const std::string &name) const;

    AsyncLogger::Ptr getDefaultLogger() const { return m_defaultLogger; }


private:

    LogManager() : m_defaultLogger(AsyncLogger::BuildAsyncLogger()) {}

    DLOG_API_EXPORTED ~LogManager();


    mutable std::shared_mutex m_mutex;

    // 固定的默认终端日志器，独立于命名表。
    AsyncLogger::Ptr m_defaultLogger;

    // 注册进命名表的其他日志器。
    std::unordered_map<std::string, AsyncLogger::Ptr> m_loggers;
};


// XXX
// 原先 GetInstance() 定义在 cpp 中，局部静态 manager 属于 dlog DLL。MSVC 的 DLL 有自己的 CRT 退出回调表，静态对象的析构以及在 DLL 中调用 std::atexit() 注册的回调，都在 DLL 卸载时执行。主程序正常退出会先执行 EXE 的 CRT 收尾，再进入 Windows 的 ExitProcess；后者先终止其他线程，随后才发送 DLL_PROCESS_DETACH。所以 DLL 卸载时机是晚于 dlog Worker 线程的释放时机的。此时再由 Manager 析构调用 stop()，Worker 可能已经被系统终止，无法继续排空队列；即使 join() 返回，也不代表尾部数据已写出，若被终止的线程持有收尾所需的锁，还可能导致死锁。
// 现在把 GetInstance() 和局部静态对象定义在头文件中，并取消 LogManager 的类级导出，仅为需要跨 DLL 调用的方法及析构函数保留 DLOG_API_EXPORTED。不能只移动函数定义：类级 dllexport/dllimport 会覆盖 GetInstance()，MSVC 的导入内联函数仍共享 DLL 中的静态对象。当前 GetInstance() 不导入 DLL，EXE 使用时会在 EXE 内创建实例并登记自动析构，因此 Manager 能在 ExitProcess 终止 Worker 之前执行 stop()，等待排空后再退出。析构函数的实现仍可放在 DLL 的 cpp 中；决定执行时机的是实例及其析构登记所在的模块。业务代码只需调用 GetInstance()，清理仍由 Manager 自己的析构函数负责。
inline LogManager &LogManager::GetInstance()
{
    static LogManager manager;
    return manager;
}


DLOG_NAMESPACE_END


#endif
