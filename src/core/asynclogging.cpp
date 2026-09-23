#include "asynclogging.h"


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
    m_running = false;
    m_cond.notify_one();
}

void AsyncLogging::threadFunc()
{
}
