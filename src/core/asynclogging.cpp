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
        std::lock_guard<std::mutex> lock(m_mtx);

        // currentData 指向这条日志中“接下来要复制的位置”。
        const char *currentData = data;
        // remaining 是还没复制的字节数。
        size_t remaining = length;

        while (remaining > 0)
        {
            // chunkLength 取“待复制字节数”和“剩余空间”中较小的那个，保证这次不会写过缓冲区边界。
            size_t chunkLength = std::min(remaining, m_producerBuffer->avail());

            // 单条日志可能大于一个 LargeBuffer；分段写入并按顺序入队，避免 FixedBuffer 丢弃超容量数据。
            m_producerBuffer->append(currentData, chunkLength);
            currentData += chunkLength;
            remaining -= chunkLength;

            // 如果本次追加导致缓冲区装满。
            if (0 == m_producerBuffer->avail())
            {
                // 使用 std::move 把生产者缓冲区交给待处理队列，供后台线程写盘。
                m_pendingBuffers.emplace_back(std::move(m_producerBuffer));

                // 写满后分配新块，继续处理剩余字节（本版本暂不预留备用缓冲区）。
                m_producerBuffer = std::make_unique<LargeBuffer>();

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
    LogFile logFile(m_basePath, m_rollSize, m_flushInterval);

    // replaceBuffer 用于替换前台生产者缓冲区；写完一批后，后台留出一块供下一轮复用。
    BufferPtr replaceBuffer = std::make_unique<LargeBuffer>();

    // 后台在每批次中要写盘的缓冲区，实际过程中和前台 m_pendingBuffers 进行交换获得数据。
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(m_mtx);

            // m_flushInterval 为 0 会让 wait_for 立即返回并造成空转，保证至少等待 1 秒。
            // wait_for()：最多等 interval 这么久；如果待写队列有数据，或者线程已停止，就提前结束等待。
            m_cond.wait_for(lock, std::chrono::seconds(m_flushInterval > 0 ? m_flushInterval : 1), [this]
                            { return !m_pendingBuffers.empty() || !m_running; });

            // m_running == false 只表示收到停止请求，不表示日志已经写完。
            // 停止时仍需排空两处数据：m_pendingBuffers 中已交出的整块缓冲区，以及 m_producerBuffer 中未写满的尾部数据。上一轮的 buffersToWrite 在回到这里前已经写入 LogFile、清空并 flush，因此无需在退出条件中检查它。只有停止请求已到且这两处都为空时才退出；否则继续交接、写入，直到排空。
            // break 以后 lock 会自动释放。这里的 std::unique_lock 是局部对象；break 会退出整个 while 循环，离开它所在的作用域时，unique_lock 析构并自动解锁。
            if (!m_running && m_pendingBuffers.empty() && 0 == m_producerBuffer->length()) break;

            // 把生产者缓冲区交给后台，并尝试用 replaceBuffer 立即给前台换一块新的。
            m_pendingBuffers.emplace_back(std::move(m_producerBuffer));
            m_producerBuffer = replaceBuffer ? std::move(replaceBuffer) : std::make_unique<LargeBuffer>();

            // 把待处理队列交给后台本地批次。解锁后前台可以继续填充新的 m_pendingBuffers。
            buffersToWrite.swap(m_pendingBuffers);
        }

        // 磁盘写入放在锁外，避免阻塞前台 append()。
        for (auto &buffer : buffersToWrite)
        {
            if (buffer && buffer->length() > 0) logFile.append(buffer->data(), buffer->length());
        }

        // 本批次已写完，留下一块供下一轮替换前台缓冲区，其余释放，不额外维护缓冲池。
        if (buffersToWrite.size() > 1) buffersToWrite.resize(1);

        if (!replaceBuffer && !buffersToWrite.empty())
        {
            replaceBuffer = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            replaceBuffer->reset();
        }

        buffersToWrite.clear();

        // 每批写完后刷新；stop() 唤醒线程后会继续处理，直到待处理数据全部清空。
        logFile.flush();
    }

    // 退出前再做一次最终刷新，将 LogFile 内部缓冲中的数据提交到文件层。
    logFile.flush();
}
