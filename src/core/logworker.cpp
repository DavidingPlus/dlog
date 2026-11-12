#include "logworker.h"
#include "threadpool.h"

#include <algorithm>
#include <stdexcept>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


LogWorker::LogWorker(OutputFunc output, std::shared_ptr<ThreadPool> threadPool)
    : m_output(std::move(output)), m_threadPool(std::move(threadPool)), m_producerBuffer(std::make_unique<LargeFixedBuffer>())
{
    if (!m_output) throw std::invalid_argument("LogWorker::constructor(): output must not be empty");
    if (!m_threadPool) throw std::invalid_argument("LogWorker::constructor(): thread pool must not be empty");

    m_pendingBuffers.reserve(16);
}

void LogWorker::scheduleTaskLocked()
{
    if (m_scheduled || (m_pendingBuffers.empty() && 0 == m_producerBuffer->readableBytes())) return;

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
            // 先准备替换块，分配失败时仍保有当前生产者缓冲区。
            auto nextBuffer = std::make_unique<LargeFixedBuffer>();
            m_pendingBuffers.emplace_back(std::move(m_producerBuffer));
            m_producerBuffer = std::move(nextBuffer);
        }
    }

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
    m_cond.wait(lock, [this]
                { return !m_scheduled; });
}

void LogWorker::threadFunc()
{
    try
    {
        // replaceBuffer 用于替换前台生产者缓冲区；写完一批后，后台留出一块供下一轮复用。
        BufferPtr replaceBuffer = std::make_unique<LargeFixedBuffer>();

        // 后台在每批次中要写盘的缓冲区，实际过程中和前台 m_pendingBuffers 进行交换获得数据。
        BufferVector buffersToWrite;
        buffersToWrite.reserve(16);

        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(m_mtx);

                // 没有待写块和尾部数据时，在同一把锁下清除任务状态，避免漏掉后续追加。
                if (m_pendingBuffers.empty() && 0 == m_producerBuffer->readableBytes())
                {
                    m_scheduled = false;
                    m_cond.notify_all();
                    return;
                }

                // 把未写满的生产者缓冲区也交给后台，并用 replaceBuffer 给前台替换。
                if (m_producerBuffer->readableBytes() > 0)
                {
                    if (!replaceBuffer) replaceBuffer = std::make_unique<LargeFixedBuffer>();
                    m_pendingBuffers.emplace_back(std::move(m_producerBuffer));
                    m_producerBuffer = std::move(replaceBuffer);
                }

                // 把待处理队列交给后台本地批次。解锁后前台可以继续填充新的 m_pendingBuffers。
                buffersToWrite.swap(m_pendingBuffers);
            }

            // 每块作为一个输出批次，在锁外完成写入和刷新，避免阻塞前台 append()。
            for (auto &buffer : buffersToWrite)
            {
                if (buffer && buffer->readableBytes() > 0) m_output(*buffer);
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
    catch (...)
    {
        std::lock_guard<std::mutex> lock(m_mtx);

        m_backgroundError = std::current_exception();

        m_running = false;
        m_scheduled = false;

        // 已输出的前缀无法回滚，放弃剩余数据，避免重试造成重复写入。
        m_pendingBuffers.clear();
        m_producerBuffer->clear();

        m_cond.notify_all();
    }
}


DLOG_NAMESPACE_END
