#include "threadpool.h"


DLOG_NAMESPACE_BEGIN(dlog)


ThreadPool::ThreadPool(size_t threadCount)
    : m_running(true)
{
    if (0 == threadCount) throw std::invalid_argument("ThreadPool::ThreadPool(): thread count must be greater than zero");

    m_workers.reserve(threadCount);

    // 这里的异常处理合理的，而且有必要。当某个线程启动失败时，前面启动的线程可能已经在 run() 中等待。如果构造函数直接抛出异常，ThreadPool 的析构函数不会被调用，Thread 析构时会对仍可 join 的线程执行 detach()，不会等待它退出。这样线程池的成员可能已经销毁，后台线程却还在访问它们。
    // stopAndJoin() 会将 m_running 设为 false。唤醒等待中的工作线程。等待已经启动的线程退出。通过 throw 继续向调用者抛出原异常。没有启动的 Thread 对象也会被遍历，但它的 join() 会直接返回，所以可以统一处理。
    try
    {
        for (size_t i = 0; i < threadCount; ++i)
        {
            m_workers.emplace_back(std::make_unique<Thread>([this]
                                                            { run(); },
                                                            "ThreadPool"));

            m_workers.back()->start();
        }
    }
    catch (...)
    {
        // 创建或启动失败时，先停止并等待已启动的线程，避免它们在构造失败后访问已销毁的成员。
        stopAndJoin(), throw;
    }
}

void ThreadPool::run()
{
    while (true)
    {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            // 等待任务队列不为空或线程池停止。
            m_condition.wait(lock, [this]
                             { return !m_running || !m_tasks.empty(); });
            if (!m_running && m_tasks.empty()) return;

            // 取出任务。
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        // 执行任务。
        task();
    }
}

void ThreadPool::stopAndJoin()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 协调条件变量等待和任务入队，防止漏掉唤醒或在停止后提交任务。
        m_running = false;
    }

    m_condition.notify_all();

    for (auto &worker : m_workers) worker->join();
}


DLOG_NAMESPACE_END
