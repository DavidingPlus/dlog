#ifndef _DLOG_CURRENTTHREAD_H_
#define _DLOG_CURRENTTHREAD_H_

#include "globalmacros.h"


namespace CurrentThread
{
    // 保存 tid 缓存，因为系统调用非常耗时，拿到 tid 后将其保存。
    // Windows DLL 不能直接通过 dllimport/dllexport 暴露 thread_local 数据，
    // 因此缓存只在 DLL 内部保存，通过函数访问。
    D_API_EXPORTED int &cachedTid() noexcept;

    D_API_EXPORTED void cacheTid();

    // 获取当前线程 tid。Windows DLL 中通过导出函数访问内部 thread_local 缓存。
    D_API_EXPORTED int tid() noexcept;
}


#endif
