#ifndef _DLOG_THREAD_H_
#define _DLOG_THREAD_H_

#include "globalmacros.h"

#include <functional>
#include <thread>
#include <memory>
#include <string>
#include <atomic>
#include <utility>


class Thread
{

    D_CLASS_NONCOPYABLE(Thread)

public:

    using ThreadFunc = std::function<void()>;

    explicit Thread(ThreadFunc func, const std::string &name = std::string()) : m_func(std::move(func)), m_name(name) { setDefaultName(); }

    ~Thread();

    void start();

    void join();

    bool started() { return m_started; }

    int tid() const { return m_tid; }

    const std::string &name() const { return m_name; }

    static int NumCreated() { return m_numCreated; }


private:

    void setDefaultName();

    // 已创建线程数量。static 达标所有 Thread 对象共享。用 atomic 保证多线程环境下保证递增操作安全。
    static std::atomic_int m_numCreated;


    // 标记线程是否已经启动。
    bool m_started = false;

    // 标记线程是否已经 join。
    bool m_joined = false;

    std::shared_ptr<std::thread> m_thread;

    // 系统线程 ID，在线程创建时绑定。
    int m_tid = 0;

    // 线程回调函数。
    ThreadFunc m_func;

    // 线程名称。
    std::string m_name;
};


#endif
