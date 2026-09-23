#include "thread.h"

#include "currentthread.h"

#include <future>


std::atomic_int Thread::m_numCreated{0};


Thread::~Thread()
{
    // thread 类提供了设置分离线程的方法。线程运行后自动销毁（非阻塞）。C++ std::thread 中 join() 和 detach() 的区别：https://blog.nowcoder.net/n/8fcd9bb6e2e94d9596cf0a45c8e5858a
    // 对象销毁时若线程仍可 join，则分离线程，避免 joinable 的 std::thread 析构导致 terminate。
    if (m_thread && m_thread->joinable()) m_thread->detach();
}

void Thread::start()
{
    // std::thread 在执行结束后、join 前仍是 joinable；此时不能覆盖它再创建新线程。join() 后 joinable() 为 false，允许该 Thread 对象重新启动。
    if (m_thread && m_thread->joinable()) return;

    // 主线程调用 start() 创建子线程后，需要等待子线程完成初始化并保存自己的 tid。使用标准 C++ future 同步，避免依赖 POSIX semaphore。
    std::promise<void> prom;
    std::future<void> fut = prom.get_future();

    // 开启线程。
    m_thread = std::make_shared<std::thread>([this, prom = std::move(prom)]() mutable
                                             {
                                                 // 获取当前线程的 tid。
                                                 // 注意：std::thread::id 是 C++ 层面的线程 ID，而这里获取的是平台自身的线程 ID，Windows 上是 ::GetCurrentThreadId()，Linux 上是 ::gettid()。
                                                 m_tid = CurrentThread::tid();

                                                 // 在执行用户回调前通知 start()，避免回调执行时间过长阻塞 start()。通知主线程，当前线程已经完成初始化，m_tid 已经设置完成。
                                                 // 如果在 Linux 下使用 sem_post，会让信号量值 +1，从而唤醒阻塞在 sem_wait 上的主线程。
                                                 prom.set_value();

                                                 // 开启一个新线程，执行该线程函数。注意必须放在 m_func() 前面，否则如果线程函数执行时间较长，start() 会一直阻塞等待，无法及时返回。
                                                 m_func(); //
                                             });
    m_started = true;

    // 等待新创建的线程完成初始化。如果不等待，主线程可能在子线程还没有执行到 CurrentThread::tid() 时，就返回 start()，此时 m_tid 仍然没有有效值。
    fut.get();
}

void Thread::join()
{
    // 允许未启动或重复 join；只有 joinable 的线程才能调用 std::thread::join()。
    if (!m_thread || !m_thread->joinable()) return;

    m_thread->join();

    m_started = false;
}

void Thread::setDefaultName()
{
    int num = ++m_numCreated;

    if (m_name.empty()) m_name = "Thread" + std::to_string(num);
}
