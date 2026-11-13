#include "logworker.h"
#include "logflush.h"
#include "logstream.h"
#include "threadpool.h"

#include <stdexcept>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


LogWorker::LogWorker(std::shared_ptr<LogFlush> logFlush, std::shared_ptr<ThreadPool> threadPool)
    : m_logFlush(std::move(logFlush)), m_threadPool(std::move(threadPool)), m_producerBuffer(std::make_unique<FlexibleBuffer>()), m_consumerBuffer(std::make_unique<FlexibleBuffer>())
{
    if (!m_logFlush) throw std::invalid_argument("LogWorker::constructor(): output must not be empty");
    if (!m_threadPool) throw std::invalid_argument("LogWorker::constructor(): thread pool must not be empty");
}

void LogWorker::append(const LogStream &stream)
{
    const auto &buffer = stream.buffer();
    append(buffer.readableBegin(), buffer.readableBytes());
}

void LogWorker::scheduleTaskLocked()
{
    if (m_scheduled || 0 == m_producerBuffer->readableBytes()) return;

    // 状态与任务提交在同一把锁下协调，避免并发追加重复安排输出。
    m_scheduled = true;

    try
    {
        m_threadPool->enqueue([this]
                              { threadFunc(); });
    }
    catch (...)
    {
        // 提交失败时撤销任务状态，保存原异常供后续调用报告。
        m_backgroundError = std::current_exception();

        m_scheduled = false;
        m_running = false;

        throw;
    }
}

void LogWorker::append(const char *data, size_t length)
{
    if (!data || 0 == length) return;

    // 持锁完成整条日志的分段追加，避免并发调用把不同日志交错到缓冲区中。
    std::lock_guard<std::mutex> lock(m_mtx);

    if (m_backgroundError) std::rethrow_exception(m_backgroundError);

    // 可增长缓冲区先保证本次追加所需空间，再复制完整数据，避免不同日志交错。
    m_producerBuffer->append(data, length);

    // 非空追加后安排任务；已有任务时由该任务继续处理新数据。
    if (m_running) scheduleTaskLocked();
}

void LogWorker::start()
{
    // 启停由调用方串行执行：重复 start 沿用当前状态，stop 完成后可再次 start。
    // start 前或 stop 后追加的数据保留在缓冲区，启动时安排输出。
    std::lock_guard<std::mutex> lock(m_mtx);

    if (m_backgroundError) std::rethrow_exception(m_backgroundError);

    if (m_running) return;
    m_running = true;

    // start() 里调用 scheduleTaskLocked()，是为了处理启动之前已经追加到缓冲区的数据。
    // worker.append("hello", 5); // 还没启动，只放进缓冲区。
    // worker.start();            // 启动后安排输出之前的数据。
    // 如果启动时没有数据，scheduleTaskLocked() 会直接返回，不会提交空任务。
    scheduleTaskLocked();
}

void LogWorker::stop()
{
    stopAndWait();

    // 等待完成后，后台任务不再修改 m_backgroundError，遇到后台错误正常抛出。
    std::lock_guard<std::mutex> lock(m_mtx);

    if (m_backgroundError) std::rethrow_exception(m_backgroundError);
}

void LogWorker::stopAndWait()
{
    // 修改状态和检查等待条件使用同一把锁，避免任务完成通知与等待之间出现遗漏。
    std::unique_lock<std::mutex> lock(m_mtx);

    m_running = false;

    // wait 期间释放 m_mtx，后台任务才能获取锁、处理尾部数据并结束。
    m_taskDoneCond.wait(lock, [this]
                        { return !m_scheduled; });
}

void LogWorker::threadFunc()
{
    try
    {
        // 双缓冲 m_producerBuffer 和 m_consumerBuffer 通过交换指针交接数据。假设两块缓冲区分别为 A 和 B。
        // 1. 前台将日志追加到 A；此时 m_producerBuffer 指向 A，m_consumerBuffer 指向已清空的 B。
        // 2. 后台持有 m_mtx，然后交换两个指针：m_consumerBuffer 指向有数据的 A，m_producerBuffer 指向空的 B。
        // 3. 后台解锁并输出 A；与此同时，前台的 m_producerBuffer 为空了，可以持锁向 B 追加新日志，不会改动正在输出的 A。此时 m_scheduled 仍为 true，前台只追加数据，不会为同一个 Worker 重复提交任务。
        // 4. A 输出完毕后清空。如果 B 有新日志，在进入下一轮：前台写入空的 A，后台输出 B。
        // 5. 直到生产者缓冲区为空，就清除 m_scheduled 并结束任务；之后有新日志时再提交任务，不占用池线程等待。
        // 因此下面的这套逻辑是 while(true) 和 if (0 == m_producerBuffer->readableBytes()) 退出循环，就是为了让前台可以合并日志请求到同一个任务中。

        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(m_mtx);

                // 没有待写数据时，结束任务。与 append() 使用同一把锁：若追加先拿锁，任务会看到新数据并继续；若任务先确认为空并清除标志，之后的追加会看到 m_scheduled == false，提交新任务。
                if (0 == m_producerBuffer->readableBytes())
                {
                    m_scheduled = false;
                    m_taskDoneCond.notify_all();
                    return;
                }

                // 把生产者缓冲区交给后台，并将已清空的消费者缓冲区换给前台。
                m_producerBuffer.swap(m_consumerBuffer);
            }

            // 在锁外输出当前批次，前台可以继续向另一块缓冲区追加日志。
            m_logFlush->writeAndFlush(m_consumerBuffer->readableBegin(), m_consumerBuffer->readableBytes());

            // 本批次已写完，清空消费者缓冲区以供下一轮交换复用。
            m_consumerBuffer->clear();
        }
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(m_mtx);

        m_backgroundError = std::current_exception();

        m_running = false;
        m_scheduled = false;

        // 已输出的前缀无法回滚，放弃剩余数据，避免重试造成重复写入。
        m_producerBuffer->clear();
        m_consumerBuffer->clear();

        m_taskDoneCond.notify_all();
    }
}


DLOG_NAMESPACE_END
