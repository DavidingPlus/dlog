#ifndef _DLOG_THREADPOOL_H_
#define _DLOG_THREADPOOL_H_

#include "thread.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <type_traits>
#include <vector>


DLOG_NAMESPACE_BEGIN(dlog)


// 参考：[https://github.com/progschj/ThreadPool](https://github.com/progschj/ThreadPool)
class DLOG_API_EXPORTED ThreadPool
{

    DLOG_CLASS_NONCOPYABLE(ThreadPool)

public:

    // 初始化线程池，启动指定数量的线程。
    // 对于每个线程，使用 Thread 创建一个新线程，并将一个 lambda 函数作为线程的执行体。在 lambda 函数中，线程会进入一个无限循环，不断尝试从任务队列中获取任务。使用 std::unique_lock<std::mutex> 加锁，确保线程安全地访问任务队列。调用 m_taskCond.wait(lock, [this]{ return !m_running || !m_tasks.empty(); }) 使线程进入等待状态，直到满足以下两个条件之一：线程池停止（m_running 为 false）或任务队列不为空。如果线程池停止且任务队列为空，线程会退出循环。从任务队列中取出一个任务，并将其移动到局部变量 task 中，然后解锁。执行取出的任务。
    explicit ThreadPool(size_t threadCount);

    // 停止接收任务，等待已提交任务执行完毕并回收所有工作线程。线程池必须由池外线程销毁，且销毁前调用方应停止提交任务。
    ~ThreadPool() { stopAndJoin(); }

    // 将一个新任务添加到任务队列中，并返回一个 std::future 对象，用于获取任务的执行结果。
    // 使用 std::packaged_task 将传入的函数 f 和参数 args 打包成一个可调用对象。通过 task->get_future() 获取一个 std::future 对象，用于异步获取任务的返回值。使用 std::unique_lock<std::mutex> 加锁，确保线程安全地访问任务队列。检查线程池是否已经停止，如果停止则抛出异常。将打包好的任务封装成一个 lambda 函数，并添加到任务队列中。调用 condition.notify_one() 唤醒一个等待的线程，通知它有新任务可用。最后返回 std::future 对象。
    template <class F, class... Args>
    std::future<std::invoke_result_t<F, Args...>> enqueue(F &&func, Args &&...args);


private:

    friend class LogManager;


    void run();

    // 等待队列和正在执行的任务均为空，但不停止线程池。
    void waitUntilIdle();

    void stopAndJoin();


    // 线程们。
    std::vector<std::unique_ptr<Thread>> m_workers;

    // 任务队列。
    std::queue<std::function<void()>> m_tasks;

    // 任务队列的互斥锁。
    std::mutex m_mutex;

    // 条件变量，用于线程等待新任务，或在线程池停止时唤醒线程。
    std::condition_variable m_taskCond;

    // 条件变量，用于等待线程池空闲，即任务队列为空且没有正在执行的任务。
    std::condition_variable m_idleCond;

    size_t m_activeTasks = 0;

    bool m_running = false;
};


template <class F, class... Args>
std::future<std::invoke_result_t<F, Args...>> ThreadPool::enqueue(F &&func, Args &&...args)
{
    // 推导以 Args... 调用 F 时的返回类型。std::invoke_result_t<F, Args...> 等价于 std::invoke_result<F, Args...>::type。
    // 参考实现中 std::result_of<F(Args...)>::type 在 C++17 中弃用、C++20 中移除。
    using ReturnType = std::invoke_result_t<F, Args...>;

    // std::packaged_task 可以理解为一个带有结果通道的任务包装器。它包装一个可调用对象，执行时会把返回值或异常保存到共享状态中，供另一端的 std::future 获取。
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::bind(std::forward<F>(func), std::forward<Args>(args)...));
    auto res = task->get_future();

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_running) throw std::runtime_error("ThreadPool: enqueue on stopped pool");

        m_tasks.emplace([task]
                        { (*task)(); });
    }

    m_taskCond.notify_one();


    return res;
}


DLOG_NAMESPACE_END


#endif
