#include "log.h"
#include "fileutil.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif


// 稳定时钟只用于计算时间差，不受系统时间调整影响。
using Clock = std::chrono::steady_clock;


// 计数输出只统计后台收到的记录和字节，不写文件。
struct Counts
{
    size_t records = 0;

    size_t bytes = 0;

    size_t batches = 0;
    size_t expected = 0;
    Clock::time_point completed;
};


class CountingFlush : public dlog::LogFlush
{

public:

    explicit CountingFlush(std::shared_ptr<Counts> counts, unsigned delayMs = 0) : m_counts(std::move(counts)), m_delayMs(delayMs) {}

    void writeAndFlush(const char *data, size_t length) override
    {
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


// Manager 保留日志器到析构；通过共享的句柄在测试成功后提前关闭文件。
class BenchFileFlush : public dlog::LogFlush
{


public:

    BenchFileFlush(const std::filesystem::path &file, std::shared_ptr<std::unique_ptr<dlog::FileFlush>> output) : m_output(std::move(output)) { *m_output = std::make_unique<dlog::FileFlush>(file); }

    void writeAndFlush(const char *data, size_t length) override { (*m_output)->writeAndFlush(data, length); }


private:

    std::shared_ptr<std::unique_ptr<dlog::FileFlush>> m_output;
};


struct Rates
{
    double submit;
    double total;
};

Rates runCase(const std::string &name, const dlog::Logger::SharedPtr &logger, size_t records, size_t threadCount, const std::shared_ptr<Counts> &counts, const std::filesystem::path &file = {})
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

    // stop() 返回后后台不再写入，此时再统计数据不会影响计时。
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

    // 提交吞吐量反映前台写入速度；总吞吐量还包括等待后台排空的时间。
    double submitSeconds = std::chrono::duration<double>(submitted - begin).count();
    double totalSeconds = std::chrono::duration<double>(drained - begin).count();

    std::cout << "\n测试场景：" << name << "\n"
              << "  生产者线程数：" << threadCount << "\n"
              << "  输出记录数：" << actualRecords << " 条\n"
              << "  输出字节数：" << actualBytes << " 字节\n"
              << "  提交耗时：" << submitSeconds << " 秒\n"
              << "  总耗时（含排空）：" << totalSeconds << " 秒\n"
              << "  提交后等待排空：" << totalSeconds - submitSeconds << " 秒\n"
              << "  提交吞吐量：" << records / submitSeconds << " 条/秒\n"
              << "  总吞吐量（含排空）：" << records / totalSeconds << " 条/秒\n";
    if (counts) std::cout << "  后台输出批次数：" << counts->batches << "\n";
    return {records / submitSeconds, records / totalSeconds};
}

double median(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    size_t mid = values.size() / 2;
    return values.size() % 2 ? values[mid] : values[mid - 1] / 2 + values[mid] / 2;
}

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
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - m_state->started).count());
        m_state->received.notify_one();
    }

private:

    std::shared_ptr<LatencyState> m_state;
};

void runLatency(const dlog::Logger::SharedPtr &logger, const std::shared_ptr<LatencyState> &state, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->started = Clock::now();
        }
        DLOG_LOGGER_INFO(logger) << "低频延迟测试 " << i;
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->received.wait_for(lock, std::chrono::seconds(5), [&]
                                      { return state->samples.size() > i; }))
            throw std::runtime_error("低频延迟测试等待后台回调超时");
    }
    logger->stop();
    auto samples = state->samples;
    if (samples.size() != count) throw std::runtime_error("低频延迟测试样本数量不符");
    std::sort(samples.begin(), samples.end());
    std::cout << "\n低频输出到回调的延迟：样本=" << count
              << "，平均=" << std::accumulate(samples.begin(), samples.end(), 0.0) / count
              << " 毫秒，P95=" << samples[(count * 95 + 99) / 100 - 1]
              << " 毫秒，最大=" << samples.back() << " 毫秒\n";
}

void runSharedPool(const dlog::Logger::SharedPtr &slow, const dlog::Logger::SharedPtr &fast,
                   const std::shared_ptr<Counts> &slowCounts, const std::shared_ptr<Counts> &fastCounts, size_t records)
{
    auto begin = Clock::now();
    for (size_t i = 0; i < records; ++i)
    {
        DLOG_LOGGER_INFO(slow) << "慢日志 " << i;
        DLOG_LOGGER_INFO(fast) << "普通日志 " << i;
    }
    auto submitted = Clock::now();
    fast->stop();
    slow->stop();
    auto allDone = Clock::now();
    if (slowCounts->records != records || fastCounts->records != records)
        throw std::runtime_error("共享线程池测试记录数不符");
    std::cout << "\n共享线程池（两个日志器各 " << records << " 条，慢输出每批等待 1 毫秒）：\n"
              << "  提交耗时：" << std::chrono::duration<double>(submitted - begin).count() << " 秒\n"
              << "  普通日志器收到全部记录（从开始计）：" << std::chrono::duration<double>(fastCounts->completed - begin).count() << " 秒\n"
              << "  慢日志器收到全部记录（从开始计）：" << std::chrono::duration<double>(slowCounts->completed - begin).count() << " 秒\n"
              << "  两者全部排空耗时：" << std::chrono::duration<double>(allDone - begin).count() << " 秒\n"
              << "  慢输出批次数：" << slowCounts->batches << "，普通输出批次数：" << fastCounts->batches << '\n';
}


int main(int argc, char const *argv[])
{
#ifdef _WIN32
    // Windows 控制台使用 UTF-8 显示中文；退出时恢复原来的代码页。
    struct ConsoleCodePage
    {
        UINT previous = GetConsoleOutputCP();

        ConsoleCodePage()
        {
            if (previous) SetConsoleOutputCP(CP_UTF8);
        }

        ~ConsoleCodePage()
        {
            std::cout.flush();
            std::cerr.flush();
            if (previous) SetConsoleOutputCP(previous);
        }
    } consoleCodePage;
#endif

    std::filesystem::path directory;

    try
    {
        if (argc > 4) throw std::invalid_argument("用法：dlog-bench [记录数] [线程数] [重复次数]");

        // 可省略参数；指定时记录总数和生产者线程数必须为正整数。
        auto parsePositive = [](const char *text)
        {
            std::string value(text);
            if (value.empty() || std::string::npos != value.find_first_not_of("0123456789")) throw std::invalid_argument("记录数和线程数必须是正整数");

            unsigned long long result;
            try
            {
                result = std::stoull(value);
            }
            catch (const std::out_of_range &)
            {
                throw std::invalid_argument("记录数或线程数超出范围");
            }
            if (0 == result || result > static_cast<unsigned long long>(SIZE_MAX)) throw std::invalid_argument("记录数和线程数必须是范围内的正整数");
            return static_cast<size_t>(result);
        };

        size_t records = argc > 1 ? parsePositive(argv[1]) : 100000;
        size_t threads = argc > 2 ? parsePositive(argv[2]) : 4;
        size_t repetitions = argc > 3 ? parsePositive(argv[3]) : 5;

        // 每次运行创建新目录，遇到重名则尝试下一个编号，不覆盖旧文件。
        auto stamp = Clock::now().time_since_epoch().count();
        for (size_t attempt = 0; attempt < 100; ++attempt)
        {
            auto candidate = dlog::FileUtil::executableDirectory() / ("dlog-bench-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate))
            {
                directory = std::move(candidate);
                break;
            }
        }
        if (directory.empty()) throw std::runtime_error("无法创建不重名的基准测试目录");

        std::cout << "日志文件保存目录：" << directory.string() << '\n';

        auto &manager = dlog::LogManager::GetInstance();
        std::vector<std::shared_ptr<std::unique_ptr<dlog::FileFlush>>> fileOutputs;
        std::vector<std::filesystem::path> files;
        size_t caseIndex = 0;

        // 每种线程数分别测试只计数和真实写文件，两者使用不同日志器。
        for (size_t threadCount : {size_t{1}, threads})
        {
            for (bool fileCase : {false, true})
            {
                std::vector<double> submitRates, totalRates;
                for (size_t iteration = 0; iteration < repetitions; ++iteration)
                {
                    auto suffix = std::to_string(++caseIndex);
                    std::cout << "\n第 " << iteration + 1 << " / " << repetitions << " 次运行\n";
                    Rates rates;
                    if (!fileCase)
                    {
                        auto counts = std::make_shared<Counts>();
                        auto logger = manager.buildLogger<CountingFlush>("counting-" + suffix, counts);
                        rates = runCase("计数输出", logger, records, threadCount, counts);
                    }
                    else
                    {
                        auto fileName = "file-" + suffix;
                        auto file = directory / (fileName + ".log");
                        auto output = std::make_shared<std::unique_ptr<dlog::FileFlush>>();
                        fileOutputs.push_back(output);
                        files.push_back(file);
                        auto logger = manager.buildLogger<BenchFileFlush>(fileName, file, output);
                        rates = runCase("文件输出", logger, records, threadCount, {}, file);
                        output->reset();
                    }
                    submitRates.push_back(rates.submit);
                    totalRates.push_back(rates.total);
                }
                std::cout << "\n"
                          << (fileCase ? "文件输出" : "计数输出") << "，线程数 " << threadCount
                          << "：提交吞吐量中位数=" << median(submitRates) << " 条/秒，总吞吐量中位数="
                          << median(totalRates) << " 条/秒\n";
            }
        }

        auto latency = std::make_shared<LatencyState>();
        auto latencyLogger = manager.buildLogger<LatencyFlush>("latency", latency);
        runLatency(latencyLogger, latency, 100);

        auto slowCounts = std::make_shared<Counts>();
        auto slowLogger = manager.buildLogger<CountingFlush>("slow-output", slowCounts, 1);
        runCase("慢输出（每批等待 1 毫秒）", slowLogger, records, threads, slowCounts);

        auto sharedSlowCounts = std::make_shared<Counts>();
        auto sharedFastCounts = std::make_shared<Counts>();
        sharedSlowCounts->expected = sharedFastCounts->expected = records;
        auto sharedSlow = manager.buildLogger<CountingFlush>("shared-slow", sharedSlowCounts, 1);
        auto sharedFast = manager.buildLogger<CountingFlush>("shared-fast", sharedFastCounts);
        runSharedPool(sharedSlow, sharedFast, sharedSlowCounts, sharedFastCounts, records);

        dlog::LogManager::Shutdown();

        // 停止后台任务后关闭文件，只删除本次创建的文件，不递归删除其他内容。
        for (auto &output : fileOutputs) output->reset();
        for (auto &file : files) std::filesystem::remove(file);
        if (!std::filesystem::remove(directory)) throw std::runtime_error("基准测试目录不存在或无法删除");

        std::cout << "\n基准测试文件已清理。\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "基准测试失败：" << error.what() << '\n';
        if (!directory.empty()) std::cerr << "未清理的文件目录：" << directory.string() << '\n';
        return EXIT_FAILURE;
    }
}
