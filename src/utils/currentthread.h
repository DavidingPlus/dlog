#ifndef _DLOG_CURRENTTHREAD_H_
#define _DLOG_CURRENTTHREAD_H_

#include "globalmacros.h"


namespace CurrentThread
{
    // 保存 tid 缓存，因为系统调用非常耗时，拿到 tid 后将其保存。
    extern thread_local int t_cachedTid;

    void cacheTid();

    // 获取当前线程 tid。之所以定义为 inline，是因为该函数调用非常频繁（例如日志、线程库等），函数体又非常小，将其内联可以避免一次普通函数调用的开销。
    // GCC/Clang 可以使用 __builtin_expect 优化热路径；MSVC 不提供该内建函数，因此在 MSVC 下直接使用普通条件判断。
    inline int tid() noexcept
    {
#if defined(OS_LINUX)
        if (__builtin_expect(t_cachedTid == 0, false)) cacheTid();
#else
        if (0 == t_cachedTid) cacheTid();
#endif
        return t_cachedTid;
    }
}


#endif
