#include "thread.h"

#include "currentthread.h"

#include <semaphore.h>


std::atomic_int Thread::m_numCreated(0);


Thread::~Thread()
{
    // thread 类提供了设置分离线程的方法 线程运行后自动销毁（非阻塞）。
    // C++ std::thread 中 join() 和 detach() 的区别：https://blog.nowcoder.net/n/8fcd9bb6e2e94d9596cf0a45c8e5858a
    if (m_started && !m_joined) m_thread->detach();
}

void Thread::start()
{
    m_started = true;

    sem_t sem;
    // 主线程调用 start() 创建子线程后，需要等待子线程完成一些初始化工作，这里是等待子线程获取自己的 tid，并保存到 m_tid 中。
    // false 指的是不设置进程间共享。
    sem_init(&sem, false, 0);

    // 开启线程。
    m_thread = std::shared_ptr<std::thread>(new std::thread([&]()
                                                            {
                                                                // 获取当前线程的 tid。
                                                                // 注意：std::thread::id 是 C++ 层面的线程 ID，而这里获取的是 Linux 内核线程 ID（gettid）。
                                                                m_tid = CurrentThread::tid();
                                                                // 通知主线程，当前线程已经完成初始化，m_tid 已经设置完成。sem_post 会让信号量值 +1，从而唤醒阻塞在 sem_wait 上的主线程。
                                                                sem_post(&sem);
                                                                // 开启一个新线程，执行该线程函数。
                                                                // 注意：sem_post 必须放在 m_func() 前面，否则如果线程函数执行时间较长，start() 会一直阻塞等待，无法及时返回。
                                                                m_func(); //
                                                            }));

    // 等待新创建的线程完成初始化。如果不等待，主线程可能在子线程还没有执行到 CurrentThread::tid() 时，就返回 start()，此时 m_tid 仍然没有有效值。
    sem_wait(&sem);

    // 销毁信号量。
    sem_destroy(&sem);
}

void Thread::join()
{
    m_joined = true;

    m_thread->join();
}

void Thread::setDefaultName()
{
    int num = ++m_numCreated;

    if (m_name.empty()) m_name = "Thread" + std::to_string(num);
}
