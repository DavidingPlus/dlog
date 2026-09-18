#include "currentthread.h"


thread_local int CurrentThread::t_cachedTid = 0;


void CurrentThread::cacheTid()
{
    // 等价于 t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));
    // syscall(SYS_gettid) 是一个系统调用，用于获取当前线程的唯一 ID。SYS_gettid 是 Linux 特定的系统调用编号，用来获取线程 ID(TID)。pid_t 是一个数据类型，用于表示进程 ID 或线程 ID。
    if (0 == t_cachedTid) t_cachedTid = ::gettid();
}
