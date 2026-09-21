#ifndef _DLOG_CURRENTTHREAD_H_
#define _DLOG_CURRENTTHREAD_H_

#include "globalmacros.h"


namespace CurrentThread
{
    // 保存 tid 缓存，因为系统调用非常耗时，拿到 tid 后将其保存。
    // Windows 下 thread_local 变量的地址要在运行时根据当前线程和 TLS 区域确定，不能像普通全局变量一样通过 __declspec(dllimport/dllexport) 直接作为 DLL 数据接口导出，否则 MSVC 会报 C2492。因此实际的 thread_local 变量保留在 DLL 内部，对外通过函数访问当前线程对应的缓存变量的引用。
    D_API_EXPORTED int &cachedTid() noexcept;

    D_API_EXPORTED void cacheTid();

    // 获取当前线程 tid。通过导出函数访问 DLL 内部的 thread_local 缓存，避免调用方直接导入 TLS 数据。
    D_API_EXPORTED int tid() noexcept;
}


#endif
