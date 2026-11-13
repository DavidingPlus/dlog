#ifndef _DLOG_BENCH_CASES_H_
#define _DLOG_BENCH_CASES_H_

#include "log.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>


// 稳定时钟只用于计算时间差，不受系统时间调整影响。
using Clock = std::chrono::steady_clock;


// 同一 Logger 的回调串行更新计数，调用方只在 stop() 返回后读取；换行数只能核对数量。
struct Counts
{
    size_t records = 0;

    size_t bytes = 0;

    size_t batches = 0;

    // 非零时，在收到预期记录数后记录完成时间。
    size_t expected = 0;

    Clock::time_point completed;
};


class CountingFlush : public dlog::LogFlush
{

public:

    explicit CountingFlush(std::shared_ptr<Counts> counts, unsigned delayMs = 0) : m_counts(std::move(counts)), m_delayMs(delayMs) {}

    void writeAndFlush(const char *data, size_t length) override
    {
        // 延迟按回调批次计算，不是按每条记录计算。
        if (m_delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(m_delayMs));

        // 每条日志以换行结束。
        m_counts->records += static_cast<size_t>(std::count(data, data + length, '\n'));
        m_counts->bytes += length;
        ++m_counts->batches;

        if (m_counts->expected && m_counts->records >= m_counts->expected) m_counts->completed = Clock::now();
    }


private:

    std::shared_ptr<Counts> m_counts;

    unsigned m_delayMs;
};


// Manager 保留日志器到析构；runCase 停止并校验后，可通过共享句柄关闭文件。
class BenchFileFlush : public dlog::LogFlush
{


public:

    BenchFileFlush(const std::filesystem::path &file, std::shared_ptr<std::unique_ptr<dlog::FileFlush>> logFlush) : m_logFlush(std::move(logFlush)) { *m_logFlush = std::make_unique<dlog::FileFlush>(file); }

    void writeAndFlush(const char *data, size_t length) override { (*m_logFlush)->writeAndFlush(data, length); }


private:

    std::shared_ptr<std::unique_ptr<dlog::FileFlush>> m_logFlush;
};


struct Rates
{
    // 每秒提交记录数，不等待后台排空。
    double submit;

    // 每秒完成记录数，计时包含 stop() 排空。
    double total;

    // 从生产者开始到 stop() 返回的批次总秒数。
    double seconds;

    // 生产者结束后，等待后台排空所花的秒数。
    double drainSeconds;

    // 校验时实际收到或从文件中读取的字节数。
    size_t bytes;
};


struct LatencyResult
{
    // 单条日志从提交前到后台回调收到数据的延迟，单位均为毫秒。
    double meanMs, p95Ms, maxMs;
};


struct SharedResult
{
    // 从共同起点计算：提交结束、普通/慢回调收全记录、两个 Logger 排空，单位均为秒。
    double submitSeconds, fastSeconds, slowSeconds, totalSeconds;
};


// 前台记录开始时间，后台回调追加样本；两者通过同一把锁与条件变量协调。
struct LatencyState
{
    std::mutex mutex;

    std::condition_variable received;

    Clock::time_point started;

    std::vector<double> samples;
};


class LatencyFlush : public dlog::LogFlush
{

public:

    explicit LatencyFlush(std::shared_ptr<LatencyState> state) : m_state(std::move(state)) {}

    void writeAndFlush(const char *, size_t) override
    {
        // 每次只允许一条待测日志，因此这个回调对应当前 started。
        std::lock_guard<std::mutex> lock(m_state->mutex);

        m_state->samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - m_state->started).count());
        m_state->received.notify_one();
    }


private:

    std::shared_ptr<LatencyState> m_state;
};


// counts 非空时校验计数输出，否则读取 file；调用前需传入已启动且未停止的 Logger。
Rates runCase(const std::string &name, const dlog::Logger::SharedPtr &logger, size_t records, size_t threadCount, const std::shared_ptr<Counts> &counts, const std::filesystem::path &file = {});

// count 必须大于零。
LatencyResult runLatency(const dlog::Logger::SharedPtr &logger, const std::shared_ptr<LatencyState> &state, size_t count);

SharedResult runSharedPool(const dlog::Logger::SharedPtr &slow, const dlog::Logger::SharedPtr &fast, const std::shared_ptr<Counts> &slowCounts, const std::shared_ptr<Counts> &fastCounts, size_t records);


#endif
