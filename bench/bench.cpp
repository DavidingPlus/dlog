#include "bench.h"

#include <fstream>
#include <numeric>
#include <stdexcept>


Rates runCase(const std::string &name, const dlog::Logger::SharedPtr &logger, size_t records, size_t threadCount, const std::shared_ptr<Counts> &counts, const std::filesystem::path &file)
{
    // 所有生产者先等待开始信号，避免把逐个创建线程的时间算进测试。
    std::mutex mutex;
    std::condition_variable readyCond;
    std::condition_variable startCond;
    size_t ready = 0;
    bool start = false;
    std::vector<std::thread> producers;
    producers.reserve(threadCount);

    try
    {
        for (size_t thread = 0; thread < threadCount; ++thread)
            producers.emplace_back([&, thread]
                                   {
                                       {
                                           std::unique_lock<std::mutex> lock(mutex);
                                           ++ready;
                                           readyCond.notify_one();
                                           startCond.wait(lock, [&]
                                                          { return start; });
                                       }

                                       // 余数分给前几个线程，例如 10 条分给 3 个线程：4、3、3。
                                       size_t count = records / threadCount + (thread < records % threadCount ? 1 : 0);
                                       for (size_t i = 0; i < count; ++i) DLOG_LOGGER_INFO(logger) << "基准测试 线程=" << thread << " 消息=" << i; //
                                   });
    }
    catch (...)
    {
        // 线程创建失败时，先唤醒并回收已经创建的线程，再继续抛出异常。
        {
            std::lock_guard<std::mutex> lock(mutex);
            start = true;
        }

        startCond.notify_all();
        for (auto &producer : producers) producer.join();

        throw;
    }

    std::unique_lock<std::mutex> lock(mutex);

    readyCond.wait(lock, [&]
                   { return ready == threadCount; });

    // 三个时间点分别是开始、生产者提交结束、后台排空结束。
    auto begin = Clock::now();
    start = true;

    lock.unlock();

    startCond.notify_all();
    for (auto &producer : producers) producer.join();

    auto submitted = Clock::now();
    logger->stop();
    auto drained = Clock::now();

    // stop() 返回后后台不再写入，此时再统计数据不会影响计时；换行数不能证明内容和顺序正确。
    size_t actualRecords = 0;
    size_t actualBytes = 0;
    if (counts)
    {
        actualRecords = counts->records;
        actualBytes = counts->bytes;
    }
    else
    {
        std::ifstream input(file, std::ios::binary);
        if (!input) throw std::runtime_error("无法打开基准测试输出文件");

        char buffer[65536];
        while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
        {
            auto size = static_cast<size_t>(input.gcount());
            actualBytes += size;
            actualRecords += static_cast<size_t>(std::count(buffer, buffer + size, '\n'));
        }

        if (input.bad()) throw std::runtime_error("读取基准测试输出文件失败");
    }
    if (actualRecords != records) throw std::runtime_error(name + "：输出记录数与预期不一致");

    // 例如 100 条用 2 秒提交、3 秒排空：提交 50 条/秒，含排空约 33.3 条/秒。
    double submitSeconds = std::chrono::duration<double>(submitted - begin).count();
    double totalSeconds = std::chrono::duration<double>(drained - begin).count();


    return {records / submitSeconds, records / totalSeconds, totalSeconds, totalSeconds - submitSeconds, actualBytes};
}

LatencyResult runLatency(const dlog::Logger::SharedPtr &logger, const std::shared_ptr<LatencyState> &state, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        // 等上一条回调完成后再提交下一条，间隔 1 毫秒模拟低频写入。
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->started = Clock::now();
        }

        DLOG_LOGGER_INFO(logger) << "低频延迟测试 " << i;

        std::unique_lock<std::mutex> lock(state->mutex);

        // 等待时释放锁让回调写入样本；谓词也能处理通知先于等待的情况。
        if (!state->received.wait_for(lock, std::chrono::seconds(5), [&]
                                      { return state->samples.size() > i; })) throw std::runtime_error("低频延迟测试等待后台回调超时");
    }
    logger->stop();

    auto samples = state->samples;
    if (samples.size() != count) throw std::runtime_error("低频延迟测试样本数量不符");

    std::sort(samples.begin(), samples.end());


    // P95 使用向上取整的排名：100 个样本取排序后的第 95 个，即下标 94。
    return {std::accumulate(samples.begin(), samples.end(), 0.0) / count,
            samples[(count * 95 + 99) / 100 - 1], samples.back()};
}

SharedResult runSharedPool(const dlog::Logger::SharedPtr &slow, const dlog::Logger::SharedPtr &fast,
                           const std::shared_ptr<Counts> &slowCounts, const std::shared_ptr<Counts> &fastCounts, size_t records)
{
    auto begin = Clock::now();

    // 同一前台线程交替提交到两个 Logger，后台输出共享线程池。
    for (size_t i = 0; i < records; ++i)
    {
        DLOG_LOGGER_INFO(slow) << "慢日志 " << i;
        DLOG_LOGGER_INFO(fast) << "普通日志 " << i;
    }

    auto submitted = Clock::now();

    // 两个完成时间在各自回调内记录，不由这里 stop() 的先后顺序决定。
    fast->stop();
    slow->stop();

    auto allDone = Clock::now();
    if (slowCounts->records != records || fastCounts->records != records) throw std::runtime_error("共享线程池测试记录数不符");


    return {std::chrono::duration<double>(submitted - begin).count(),
            std::chrono::duration<double>(fastCounts->completed - begin).count(),
            std::chrono::duration<double>(slowCounts->completed - begin).count(),
            std::chrono::duration<double>(allDone - begin).count()};
}
