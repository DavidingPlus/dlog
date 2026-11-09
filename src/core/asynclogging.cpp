#include "asynclogging.h"

#include "logflush.h"

#include <chrono>


DLOG_NAMESPACE_BEGIN(dlog)


void AsyncLogging::append(const char *data, size_t length)
{
    if (!data || 0 == length) return;

    // 记一个标志，表示这次调用有没有把装满的缓冲区放进待写队列。解锁后，根据这个标志决定要不要唤醒后台线程。
    bool queuedBuffer = false;

    {
        // 持锁完成整条日志的分段追加，避免并发调用把不同日志交错到缓冲区中。
        std::lock_guard<std::mutex> lock(m_mtx);

        // currentData 指向这条日志中“接下来要复制的位置”。
        const char *currentData = data;
        // remaining 是还没复制的字节数。
        size_t remaining = length;

        while (remaining > 0)
        {
            // chunkLength 取“待复制字节数”和“剩余空间”中较小的那个，保证这次不会写过缓冲区边界。
            size_t chunkLength = std::min(remaining, m_producerBuffer->writableBytes());

            // 单条日志可能大于一个 LargeFixedBuffer；分段按顺序入队，保证每次追加不超过 FixedBuffer 的剩余容量。
            m_producerBuffer->append(currentData, chunkLength);
            currentData += chunkLength;
            remaining -= chunkLength;

            // 如果本次追加导致缓冲区装满。
            if (0 == m_producerBuffer->writableBytes())
            {
                // 使用 std::move 把生产者缓冲区交给待处理队列，供后台线程写盘。
                m_pendingBuffers.emplace_back(std::move(m_producerBuffer));

                // 写满后分配新块，继续处理剩余字节（本版本暂不预留备用缓冲区）。
                m_producerBuffer = std::make_unique<LargeFixedBuffer>();

                queuedBuffer = true;
            }
        }
    }

    // 有完整缓冲区可供后台写入时，解锁后唤醒消费者线程。
    if (queuedBuffer) m_cond.notify_one();
}

void AsyncLogging::start()
{
    // 已经运行时重复 start() 不再创建第二个后台线程，stop() 完成后可再次启动。
    if (m_running) return;

    // Thread::start() 创建线程时可能抛异常，catch 是为了回滚状态。如果没有回滚，m_running 会留在 true，之后再调用 start() 会误以为后台线程已经启动。
    m_running = true;
    try
    {
        m_thread.start();
    }
    catch (...)
    {
        m_running = false, throw;
    }
}

void AsyncLogging::stop()
{
    // m_running 是原子变量，单独读写它不会发生数据竞争；这里加锁是为了协调条件变量的“检查条件 -> 进入等待”。
    // notify_one() 不会保存通知：通知发生时若还没有线程等待，之后进入等待的线程不会收到这次通知。
    // 例如 m_flushInterval == 60 秒，不持锁修改停止标志时可能出现以下时序：
    // 1. 后台线程持有 m_mtx，检查到队列为空且 m_running == true，等待谓词返回 false。
    // 2. 后台还没进入等待，控制线程就将 m_running 改为 false，并调用 notify_one()；此时通知可能无人接收。
    // 3. 后台随后进入等待，可能直到 60 秒定时到期才重新检查停止标志，导致 stop() 中的 join() 额外等待。
    // atomic 只保证标志读写安全，并不能把“检查谓词”和“进入等待”合成一个与控制线程协调的过程。
    {
        // 修改标志与后台检查谓词使用同一把锁，因此只会出现以下两种正常时序：
        // 1. stop() 先拿到锁：设置 false 后解锁；后台之后检查谓词时直接看到停止请求，不再进入等待。
        // 2. 后台先拿到锁：stop() 暂时阻塞；后台通过 wait_for 原子地释放锁并进入等待后，stop() 才能拿锁设置 false。
        //    随后的 notify_one() 就能唤醒后台，消除“检查完了却还没开始等待”时遗漏停止通知的窗口。
        std::lock_guard<std::mutex> lock(m_mtx);
        m_running = false;
    }

    // 解锁后再通知，后台被唤醒后才能取得 m_mtx，继续交接尾部数据并判断退出。
    m_cond.notify_one();

    // join 必须在锁外：后台还需要 m_mtx 来排空数据并退出，持锁等待它结束会造成死锁。
    // Thread::join() 已处理未启动及重复调用，因此不需要用 m_running 提前返回。
    m_thread.join();
}

void AsyncLogging::threadFunc()
{
    RollFileFlush output(m_basePath, m_rollSize);

    // replaceBuffer 用于替换前台生产者缓冲区；写完一批后，后台留出一块供下一轮复用。
    BufferPtr replaceBuffer = std::make_unique<LargeFixedBuffer>();

    // 后台在每批次中要写盘的缓冲区，实际过程中和前台 m_pendingBuffers 进行交换获得数据。
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m_mtx);

            // m_flushInterval 为 0 会让 wait_for 立即返回并造成空转，保证至少等待 1 秒。
            // wait_for()：最多等 interval 这么久；如果待写队列有数据，或者线程已停止，就提前结束等待。
            // 谓词在持有 lock 时检查；需要等待时，wait_for 原子地释放 m_mtx 并进入等待，醒来后重新持锁检查。
            // stop() 必须用同一把 m_mtx 修改停止标志，具体的遗漏通知时序见 stop() 中的例子。
            m_cond.wait_for(lock, std::chrono::seconds(m_flushInterval > 0 ? m_flushInterval : 1), [this]
                            { return !m_pendingBuffers.empty() || !m_running; });

            // m_running == false 只表示收到停止请求，不表示日志已经写完。
            // 停止时仍需排空两处数据：m_pendingBuffers 中已交出的整块缓冲区，以及 m_producerBuffer 中未写满的尾部数据。上一轮的 buffersToWrite 已经通过输出目标写入并刷新，因此无需在退出条件中检查它。只有停止请求已到且这两处都为空时才退出；否则继续交接、写入，直到排空。
            // break 以后 lock 会自动释放。这里的 std::unique_lock 是局部对象；break 会退出整个 while 循环，离开它所在的作用域时，unique_lock 析构并自动解锁。
            if (!m_running && m_pendingBuffers.empty() && 0 == m_producerBuffer->readableBytes()) break;

            // 把生产者缓冲区交给后台，并尝试用 replaceBuffer 立即给前台换一块新的。
            m_pendingBuffers.emplace_back(std::move(m_producerBuffer));
            m_producerBuffer = replaceBuffer ? std::move(replaceBuffer) : std::make_unique<LargeFixedBuffer>();

            // 把待处理队列交给后台本地批次。解锁后前台可以继续填充新的 m_pendingBuffers。
            buffersToWrite.swap(m_pendingBuffers);
        }

        // 每块作为一个输出批次，在锁外完成写入和刷新，避免阻塞前台 append()。
        for (auto &buffer : buffersToWrite)
        {
            if (buffer && buffer->readableBytes() > 0) output.writeAndFlush(buffer->readableBegin(), buffer->readableBytes());
        }

        // 本批次已写完，留下一块供下一轮替换前台缓冲区，其余释放，不额外维护缓冲池。
        if (buffersToWrite.size() > 1) buffersToWrite.resize(1);

        if (!replaceBuffer && !buffersToWrite.empty())
        {
            replaceBuffer = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            replaceBuffer->clear();
        }

        buffersToWrite.clear();
    }
}


DLOG_NAMESPACE_END
