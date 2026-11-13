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
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>


using Clock = std::chrono::steady_clock;


struct Counts
{
    size_t records = 0;
    size_t bytes = 0;
};


class CountingFlush : public dlog::LogFlush
{
public:

    explicit CountingFlush(std::shared_ptr<Counts> counts) : m_counts(std::move(counts)) {}

    void writeAndFlush(const char *data, size_t length) override
    {
        m_counts->records += static_cast<size_t>(std::count(data, data + length, '\n'));
        m_counts->bytes += length;
    }

private:

    std::shared_ptr<Counts> m_counts;
};


void runCase(const std::string &name, const dlog::Logger::SharedPtr &logger, size_t records, size_t threadCount, const std::shared_ptr<Counts> &counts, const std::filesystem::path &file = {})
{
    std::mutex mutex;
    std::condition_variable readyCondition;
    std::condition_variable startCondition;
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
                readyCondition.notify_one();
                startCondition.wait(lock, [&] { return start; });
            }
            const size_t count = records / threadCount + (thread < records % threadCount ? 1 : 0);
            for (size_t i = 0; i < count; ++i)
                DLOG_LOGGER_INFO(logger) << "benchmark thread=" << thread << " message=" << i; });
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            start = true;
        }
        startCondition.notify_all();
        for (auto &producer : producers) producer.join();
        throw;
    }

    std::unique_lock<std::mutex> lock(mutex);
    readyCondition.wait(lock, [&]
                        { return ready == threadCount; });
    const auto begin = Clock::now();
    start = true;
    lock.unlock();
    startCondition.notify_all();
    for (auto &producer : producers) producer.join();
    const auto submitted = Clock::now();
    logger->stop();
    const auto drained = Clock::now();

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
        if (!input) throw std::runtime_error("cannot read benchmark output file");
        char buffer[65536];
        while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
        {
            const auto size = static_cast<size_t>(input.gcount());
            actualBytes += size;
            actualRecords += static_cast<size_t>(std::count(buffer, buffer + size, '\n'));
        }
        if (input.bad()) throw std::runtime_error("failed to read benchmark output file");
    }
    if (actualRecords != records) throw std::runtime_error(name + ": record count mismatch");

    const double submitSeconds = std::chrono::duration<double>(submitted - begin).count();
    const double totalSeconds = std::chrono::duration<double>(drained - begin).count();
    std::cout << name << ": threads=" << threadCount << ", records=" << actualRecords << ", bytes=" << actualBytes
              << ", submit_s=" << submitSeconds << ", total_s=" << totalSeconds
              << ", submit_records_per_s=" << records / submitSeconds
              << ", total_records_per_s=" << records / totalSeconds << '\n';
}


int main(int argc, char const *argv[])
{
    try
    {
        if (argc > 3) throw std::invalid_argument("usage: dlog-bench [records] [threads]");
        auto parsePositive = [](const char *text)
        {
            const std::string value(text);
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument("records and threads must be positive integers");
            const auto result = std::stoull(value);
            if (result == 0 || result > static_cast<unsigned long long>(SIZE_MAX))
                throw std::invalid_argument("records and threads must be positive integers");
            return static_cast<size_t>(result);
        };
        const size_t records = argc > 1 ? parsePositive(argv[1]) : 1000;
        const size_t threads = argc > 2 ? parsePositive(argv[2]) : 4;

        const auto stamp = Clock::now().time_since_epoch().count();
        std::filesystem::path directory;
        for (size_t attempt = 0; attempt < 100; ++attempt)
        {
            directory = dlog::FileUtil::executableDirectory() / ("dlog-bench-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(directory)) break;
            directory.clear();
        }
        if (directory.empty()) throw std::runtime_error("cannot create a unique benchmark directory");
        std::cout << "File output directory: " << directory.string() << '\n';

        auto &manager = dlog::LogManager::GetInstance();
        size_t caseIndex = 0;
        for (const size_t threadCount : {size_t{1}, threads})
        {
            const auto suffix = std::to_string(++caseIndex);
            const auto counts = std::make_shared<Counts>();
            const auto memoryName = "counting-" + suffix;
            auto memoryLogger = manager.buildLogger<CountingFlush>(memoryName, counts);
            runCase(memoryName, memoryLogger, records, threadCount, counts);

            const auto fileName = "file-" + suffix;
            const auto file = directory / (fileName + ".log");
            auto fileLogger = manager.buildLogger<dlog::FileFlush>(fileName, file);
            runCase(fileName, fileLogger, records, threadCount, {}, file);
        }
        dlog::LogManager::Shutdown();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "dlog-bench: " << error.what() << '\n';
        return 1;
    }
}
