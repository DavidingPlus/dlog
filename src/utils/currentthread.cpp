#include "currentthread.h"

#if defined(D_OS_WIN32)
#include <windows.h>
#elif defined(D_OS_LINUX)
#include <unistd.h>
#endif


namespace CurrentThread
{
    namespace
    {
        thread_local int t_cachedTid = 0;
    }
}


int &CurrentThread::cachedTid() noexcept
{
    return t_cachedTid;
}


void CurrentThread::cacheTid()
{
    if (0 != t_cachedTid) return;

#if defined(D_OS_WIN32)
    // Windows 的线程 ID 在进程间也是唯一的，适合作为当前线程的稳定标识。
    t_cachedTid = static_cast<int>(::GetCurrentThreadId());
#elif defined(D_OS_LINUX)
    // 等价于 t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));
    // syscall(SYS_gettid) 是一个系统调用，用于获取当前线程的唯一 ID。SYS_gettid 是 Linux 特定的系统调用编号，用来获取线程 ID(TID)。pid_t 是一个数据类型，用于表示进程 ID 或线程 ID。
    t_cachedTid = static_cast<int>(::gettid());
#endif
}


int CurrentThread::tid() noexcept
{
#if defined(D_OS_WIN32)
    if (0 == t_cachedTid) cacheTid();
#elif defined(D_OS_LINUX)
    if (__builtin_expect(t_cachedTid == 0, false)) cacheTid();
#endif
    return t_cachedTid;
}
