#include "currentthread.h"

#if defined(D_OS_WIN32)
#include <windows.h>
#elif defined(D_OS_LINUX)
#include <unistd.h>
#endif


namespace
{
    // 每个线程各自独立拥有一份缓存。该变量不作为 DLL 数据符号导出，由下面的导出函数在 DLL 内部访问。
    thread_local int t_cachedTid = 0;
}


int &CurrentThread::cachedTid() noexcept { return t_cachedTid; }

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
    // GCC/Clang 可以使用 __builtin_expect 优化热路径；MSVC 不提供该内建函数，因此在 MSVC 下直接使用普通条件判断。

#if defined(D_OS_WIN32)
    if (0 == t_cachedTid) cacheTid();
#elif defined(D_OS_LINUX)
    // __builtin_expect(expr, expected) 是 GCC/Clang 提供的编译器内建函数，用于告诉编译器某个条件大概率是否成立。这里 __builtin_expect(t_cachedTid == 0, 0) 表示编译器认为 t_cachedTid == 0 这个条件大概率为 false。因为每个线程第一次调用 tid() 后就已经完成缓存，后续绝大多数调用都会直接返回缓存值，不再进入 cacheTid()。这样可以帮助编译器优化代码布局，提高 CPU 分支预测命中率，使最常执行的路径（直接返回缓存）成为 Hot Path。
    //__builtin_expect(expr, expected)，expr：实际要判断的表达式，expected：你希望 expr 的值。
    if (__builtin_expect(t_cachedTid == 0, false)) cacheTid();
#endif


    return t_cachedTid;
}
