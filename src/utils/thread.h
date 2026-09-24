#ifndef _DLOG_THREAD_H_
#define _DLOG_THREAD_H_

#include "globalmacros.h"

#include <functional>
#include <thread>
#include <memory>
#include <string>
#include <atomic>
#include <utility>


class D_API_EXPORTED Thread
{

    D_CLASS_NONCOPYABLE(Thread)

public:

    using ThreadFunc = std::function<void()>;

    explicit Thread(ThreadFunc func, const std::string &name = std::string()) : m_func(std::move(func)), m_name(name) { setDefaultName(); }

    ~Thread();

    // 启动线程。当前线程尚未 join 时重复调用会忽略，join 后可以再次启动。
    void start();

    // 等待当前线程结束。未启动或已经 join 时调用会忽略。
    void join();

    // 当前这一轮线程是否已启动且尚未 join。join 后恢复为 false，可再次 start。
    bool started() const { return m_started; }

    int tid() const { return m_tid; }

    const std::string &name() const { return m_name; }

    static int NumCreated() { return m_numCreated; }


private:

    void setDefaultName();


    // 已创建线程数量。static 达标所有 Thread 对象共享。用 atomic 保证多线程环境下保证递增操作安全。
    static std::atomic_int m_numCreated;


    // 当前线程是否已经启动。join 后清零，允许同一对象再次启动。
    bool m_started = false;

    std::shared_ptr<std::thread> m_thread;

    // 系统线程 ID，在线程创建时绑定。
    int m_tid = 0;

    // 线程回调函数。
    ThreadFunc m_func;

    // 线程名称。
    std::string m_name;
};


#endif
