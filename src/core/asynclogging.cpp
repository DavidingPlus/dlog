#include "asynclogging.h"

#include "logfile.h"

#include <chrono>


AsyncLogging::~AsyncLogging()
{
    if (m_running) stop();
}

void AsyncLogging::append(const char *data, size_t length)
{
    if (!data || 0 == length) return;

    // 记一个标志，表示这次调用有没有把装满的缓冲区放进待写队列。解锁后，根据这个标志决定要不要唤醒后台线程。
    bool queuedBuffer = false;

    {
        // 持锁完成整条日志的分段追加，避免并发调用把不同日志交错到缓冲区中。
        std::lock_guard<std::mutex> lock(m_mutex);

        // currentData 指向这条日志中“接下来要复制的位置”。
        const char *currentData = data;
        // remaining 是还没复制的字节数。
        size_t remaining = length;

        while (remaining > 0)
        {
            // chunkLength 取“待复制字节数”和“剩余空间”中较小的那个，保证这次不会写过缓冲区边界。
            size_t chunkLength = std::min(remaining, m_currentBuffer->avail());

            // 单条日志可能大于一个 LargeBuffer；分段写入并按顺序入队，避免 FixedBuffer 丢弃超容量数据。
            m_currentBuffer->append(currentData, chunkLength);
            currentData += chunkLength;
            remaining -= chunkLength;

            // 如果本次追加导致缓冲区装满。
            if (0 == m_currentBuffer->avail())
            {
                // 使用 std::move 把 m_currentBuffer 这块缓冲区的所有权交给 m_buffers 队列，供后台线程写盘。
                m_buffers.emplace_back(std::move(m_currentBuffer));

                // 有备用缓冲区就拿来用；没有就新建一块。循环继续处理这条日志剩余的字节。
                m_currentBuffer = m_nextBuffer ? std::move(m_nextBuffer) : std::make_unique<LargeBuffer>();

                queuedBuffer = true;
            }
        }
    }

    // 有完整缓冲区可供后台写入时，解锁后唤醒消费者线程。
    if (queuedBuffer) m_cond.notify_one();
}

void AsyncLogging::start()
{
    m_running = true;
    m_thread.start();
}

void AsyncLogging::stop()
{
    // TODO code review

    if (!m_running.exchange(false)) return;

    m_cond.notify_one();

    // 等待后台线程取完剩余缓冲区并完成最后一次 flush，避免对象析构后线程继续访问 this。
    if (m_thread.started()) m_thread.join();
}

void AsyncLogging::threadFunc()
{
    // TODO code review

    LogFile output(m_basePath, m_rollSize, m_flushInterval);

    // newBuffer1 用于替换前台当前缓冲区，newBuffer2 用于补充前台备用缓冲区。它们写完一批日志后会从 buffersToWrite 中回收，减少反复分配大块内存。
    BufferPtr newBuffer1 = std::make_unique<LargeBuffer>(), newBuffer2 = std::make_unique<LargeBuffer>();

    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while (true)
    {
        bool stopping = false;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            // 有完整缓冲区时由 append() 通知；只有部分日志时定时醒来，将当前缓冲区也纳入本批。
            m_cond.wait_for(lock, std::chrono::seconds(m_flushInterval), [this]
                            { return !m_buffers.empty() || !m_running.load(); });

            // 把前台当前缓冲区交给后台，并立即给前台换一块新的。
            m_buffers.emplace_back(std::move(m_currentBuffer));
            m_currentBuffer = newBuffer1 ? std::move(newBuffer1) : std::make_unique<LargeBuffer>();

            // 前台若已用掉备用缓冲区，就补上一块，避免生产线程因后台写盘而等待分配。
            if (!m_nextBuffer) m_nextBuffer = newBuffer2 ? std::move(newBuffer2) : std::make_unique<LargeBuffer>();

            // 交换所有权而非复制日志字节。解锁后前台可以继续填充新的 m_buffers。
            buffersToWrite.swap(m_buffers);
            stopping = !m_running.load();
        }

        // 磁盘写入放在锁外，避免阻塞前台 append()。
        for (auto &buffer : buffersToWrite)
        {
            if (buffer && buffer->length() > 0) output.append(buffer->data(), buffer->length());
        }

        // 本批已写完；最多保留两块缓冲区作为后台备用，其余释放，避免突发日志造成内存长期膨胀。
        if (buffersToWrite.size() > 2) buffersToWrite.resize(2);

        if (!newBuffer1 && !buffersToWrite.empty())
        {
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }
        if (!newBuffer2 && !buffersToWrite.empty())
        {
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }
        buffersToWrite.clear();

        // 每批写完后刷新；stop() 唤醒线程后也会走完这一轮，再退出循环。
        output.flush();
        if (stopping) break;
    }

    // 退出前再做一次最终刷新。
    output.flush();
}
