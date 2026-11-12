#ifndef _DLOG_LOGMANAGER_H_
#define _DLOG_LOGMANAGER_H_

#include "logger.h"
#include "threadpool.h"

#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>


DLOG_NAMESPACE_BEGIN(dlog)


class LogManager
{

    DLOG_CLASS_NONCOPYABLE(LogManager)

public:


    static LogManager &GetInstance();

    // 可选。可在首次获取 LogManager（即 GetInstance()）前设置共享池线程数。线程数为 0 时抛出异常，LogManager 初始化后配置会警告并忽略。
    static void ConfigureThreadPool(size_t threadCount);

    // 可选。显式关闭，先停止外部生产者并结束临时日志消息，再由控制线程串行调用，不可从线程池任务或输出回调中调用。等待已提交的线程池任务，再停止并排空受管理的 Logger；关闭后不可重新使用，可重复调用。未使用过时调用会先创建实例。未注册的独立 Logger 由调用方自行停止；后台输出失败时停止其余 Logger 后重抛第一个异常。
    static void Shutdown() { GetInstance().shutdown(); }


    // 空名称或空目标抛出 invalid_argument，重复名称抛出 logic_error。不推荐用多个名称注册同一实例或注册默认实例，关闭时会重复调用 stop()，后果不可预期。
    DLOG_API_EXPORTED void registerLogger(std::string name, Logger::Ptr logger);

    DLOG_API_EXPORTED Logger::Ptr getLogger(const std::string &name) const;

    Logger::Ptr getDefaultLogger() const { return m_defaultLogger; }

    // 日志组件全局共享的线程池。
    std::shared_ptr<ThreadPool> getThreadPool() const { return m_threadPool; }


private:

    struct ThreadPoolConfig
    {
        // 这把锁在 LogManager 创建前即可使用，保护下面两个字段，协调线程数配置和首次初始化。它与 Logger 表锁各管一组数据，避免注册、查找与配置操作互相影响。
        mutable std::mutex m_configMutex;

        size_t m_threadCount = 4;

        bool m_initialized = false;
    };


    static ThreadPoolConfig &GetThreadPoolConfig();


    // 线程池由 Manager 持有，关闭时先等待池中任务，再停止已管理的 Logger。
    // GetInstance() 中调用构造函数，在外部已对 m_configMutex，这里不考虑竞争问题。
    explicit LogManager(ThreadPoolConfig &config) : m_threadPool(std::make_shared<ThreadPool>(config.m_threadCount)), m_defaultLogger(Logger::BuildLogger()) { config.m_initialized = true; }

    ~LogManager() { shutdown(); }

    DLOG_API_EXPORTED void shutdown();


    // 保护日志器表和关闭状态；Manager 创建前无法使用这把成员锁。
    mutable std::shared_mutex m_loggerMapMutex;

    bool m_shutdown = false;

    std::shared_ptr<ThreadPool> m_threadPool;

    // 固定的默认终端日志器，独立于命名表。
    Logger::Ptr m_defaultLogger;

    // 注册进命名表的其他日志器。
    std::unordered_map<std::string, Logger::Ptr> m_loggers;
};


// XXX
// 原先 GetInstance() 定义在 cpp 中，局部静态 manager 属于 dlog DLL。MSVC 的 DLL 有自己的 CRT 退出回调表，静态对象的析构以及在 DLL 中调用 std::atexit() 注册的回调，都在 DLL 卸载时执行。主程序正常退出会先执行 EXE 的 CRT 收尾，再进入 Windows 的 ExitProcess；后者先终止其他线程，随后才发送 DLL_PROCESS_DETACH。所以 DLL 卸载时机是晚于 dlog Worker 线程的释放时机的。此时再由 Manager 析构调用 stop()，Worker 可能已经被系统终止，无法继续排空队列；即使 join() 返回，也不代表尾部数据已写出，若被终止的线程持有收尾所需的锁，还可能导致死锁。
// 现在把 GetInstance() 和局部静态对象定义在头文件中，并取消 LogManager 的类级导出，仅为需要跨 DLL 调用的方法及析构函数保留 DLOG_API_EXPORTED。不能只移动函数定义：类级 dllexport/dllimport 会覆盖 GetInstance()，MSVC 的导入内联函数仍共享 DLL 中的静态对象。当前 GetInstance() 不导入 DLL，EXE 使用时会在 EXE 内创建实例并登记自动析构，因此 Manager 能在 ExitProcess 终止 Worker 之前执行 stop()，等待排空后再退出。析构函数的实现仍可放在 DLL 的 cpp 中；决定执行时机的是实例及其析构登记所在的模块。业务代码只需调用 GetInstance()，清理仍由 Manager 自己的析构函数负责。
// 因此下面三个函数都保留在头文件中，调整时要同时考虑 Manager 生命周期和配置状态，不能只把其中一个定义移入 DLL。
// 1. GetInstance() 的局部静态 manager 决定 Manager 实例及析构登记所在模块；EXE 调用时保留在 EXE，避免上述 DLL 卸载时才清理线程的问题。
// 2. GetThreadPoolConfig() 的局部静态 config 保存线程数、初始化状态和锁，ConfigureThreadPool() 修改的必须是 GetInstance() 读取的同一个 config。如果单独把配置入口或配置对象移入 DLL，可能使 EXE 和 DLL 访问不同的状态，导致配置不生效或初始化后的检查失效；若要拆分，须明确传递或统一管理配置。
// 3. 三个定义均使用 inline，以允许同一链接模块的多个翻译单元包含这些定义，并共享对应的局部静态对象；inline 不要求编译器实际展开函数。
// EXE 与各业务 DLL 可以分别持有和管理自己的 Manager；不要求它们跨模块共享同一实例。
// 上述讨论的是 Windows 业务 exe 的生命周期。但还有第二关，Windows 业务 DLL 内的 Manager 在进程退出时可能无法排空尾部日志，因为无论如何设计，Manager 都是属于业务 DLL 而非业务 DLL 交予的 exe 的，详见 docs/windows-dll-exit.md。目前同 spdlog，显式提供 Shutdown() 函数，Windows 业务 DLL 应在正常业务退出或卸载前主动调用 Shutdown()，若等到进程退出时才自动析构，仍可能无法排空尾部日志。

inline LogManager::ThreadPoolConfig &LogManager::GetThreadPoolConfig()
{
    static ThreadPoolConfig config;
    return config;
}

inline void LogManager::ConfigureThreadPool(size_t threadCount)
{
    auto &config = GetThreadPoolConfig();

    {
        std::lock_guard<std::mutex> lock(config.m_configMutex);

        if (!config.m_initialized)
        {
            if (0 == threadCount) throw std::invalid_argument("LogManager: thread count must be greater than zero");
            config.m_threadCount = threadCount;
            return;
        }
    }

    // 避免通过日志接口输出警告，递归调用 LogManager。
    std::cerr << "LogManager::ConfigureThreadPool(): thread pool is already initialized; configuration ignored\n";
}

inline LogManager &LogManager::GetInstance()
{
    static LogManager manager = []
    {
        auto &config = GetThreadPoolConfig();
        // 外部加锁，构造函数内部不加锁。
        std::lock_guard<std::mutex> lock(config.m_configMutex);
        return LogManager(config);
    }();
    return manager;
}


DLOG_NAMESPACE_END


#endif
