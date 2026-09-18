#ifndef _DLOG_CURRENTTHREAD_H_
#define _DLOG_CURRENTTHREAD_H_

#include <unistd.h>
#include <sys/syscall.h>


namespace CurrentThread
{
    // 保存 tid 缓存，因为系统调用非常耗时，拿到 tid 后将其保存。
    extern thread_local int t_cachedTid;

    void cacheTid();

    // 获取当前线程 tid。之所以定义为 inline，是因为该函数调用非常频繁（例如日志、线程库等），函数体又非常小，将其内联可以避免一次普通函数调用的开销。
    // __builtin_expect(expr, expected) 是 GCC/Clang 提供的编译器内建函数，用于告诉编译器某个条件大概率是否成立。这里 __builtin_expect(t_cachedTid == 0, 0) 表示编译器认为 t_cachedTid == 0 这个条件大概率为 false。因为每个线程第一次调用 tid() 后就已经完成缓存，后续绝大多数调用都会直接返回缓存值，不再进入 cacheTid()。这样可以帮助编译器优化代码布局，提高 CPU 分支预测命中率，使最常执行的路径（直接返回缓存）成为 Hot Path。
    inline int tid() noexcept
    {
        //__builtin_expect(expr, expected)，expr：实际要判断的表达式，expected：你希望 expr 的值。
        if (__builtin_expect(t_cachedTid == 0, false)) cacheTid();
        return t_cachedTid;
    }
}


#endif
