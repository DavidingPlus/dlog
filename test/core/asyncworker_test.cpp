#include <gtest/gtest.h>

#include "asyncworker.h"
#include "logflush.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>


using namespace dlog;
using namespace std::chrono_literals;


TEST(AsyncWorkerTest, RejectsEmptyOutput)
{
    EXPECT_THROW({ AsyncWorker worker(AsyncWorker::OutputFunc{}); }, std::invalid_argument);
}

TEST(AsyncWorkerTest, CopiesBinaryDataAcrossBlocksAndCallsOutputOnBackgroundThread)
{
    std::string input(2 * kLargeFixedBufferSize + 137, 'W');
    input[kLargeFixedBufferSize] = '\0';
    const auto expected = input;
    std::string actual;
    bool allCallsOnBackgroundThread = true;
    const auto caller = std::this_thread::get_id();
    AsyncWorker worker([&](const char *data, size_t length)
                       {
                           allCallsOnBackgroundThread &= std::this_thread::get_id() != caller;
                           actual.append(data, length); },
                       60);

    worker.append(input.data(), input.size());
    input.assign(input.size(), 'X');
    EXPECT_TRUE(actual.empty());
    worker.start();
    worker.stop();

    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(allCallsOnBackgroundThread);
}

TEST(AsyncWorkerTest, RepeatedStartStopAndAppendBetweenRunsPreserveData)
{
    std::string actual;
    AsyncWorker worker([&](const char *data, size_t length)
                       { actual.append(data, length); },
                       60);

    worker.stop();
    worker.append("before|", 7);
    worker.start();
    worker.start();
    worker.stop();
    worker.stop();
    worker.append("restart", 7);
    worker.start();
    worker.stop();

    EXPECT_EQ(actual, "before|restart");
}

TEST(AsyncWorkerTest, EmptyWritesDoNotInvokeOutput)
{
    size_t calls = 0;
    AsyncWorker worker([&](const char *, size_t)
                       { ++calls; },
                       60);
    worker.append(nullptr, 1);
    worker.append(nullptr, 0);
    worker.append("data", 0);
    worker.start();
    worker.stop();
    EXPECT_EQ(calls, 0u);
}

TEST(AsyncWorkerTest, DestructorDrainsPartialBlock)
{
    const std::string message("tail\0bytes", 10);
    std::string actual;
    {
        AsyncWorker worker([&](const char *data, size_t length)
                           { actual.append(data, length); },
                           60);
        worker.append(message.data(), message.size());
        worker.start();
    }
    EXPECT_EQ(actual, message);
}

TEST(AsyncWorkerTest, ZeroIntervalStillProcessesPartialDataPeriodically)
{
    std::promise<std::string> delivered;
    auto result = delivered.get_future();
    AsyncWorker worker([&](const char *data, size_t length)
                       { delivered.set_value(std::string(data, length)); },
                       0);
    worker.append("periodic", 8);
    worker.start();
    const auto status = result.wait_for(5s);
    worker.stop();

    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(result.get(), "periodic");
}

TEST(AsyncWorkerTest, ConcurrentLongRecordsStayWholeAcrossBlocks)
{
    constexpr size_t recordSize = kLargeFixedBufferSize + 31;
    constexpr size_t producerCount = 4;
    std::string actual;
    AsyncWorker worker([&](const char *data, size_t length)
                       { actual.append(data, length); },
                       60);
    worker.start();
    std::vector<std::thread> producers;
    for (size_t index = 0; index < producerCount; ++index)
    {
        producers.emplace_back([&, index]
                               {
                                   const std::string message(recordSize, static_cast<char>('A' + index));
                                   worker.append(message.data(), message.size()); });
    }
    for (auto &producer : producers) producer.join();
    worker.stop();

    ASSERT_EQ(actual.size(), producerCount * recordSize);
    bool seen[producerCount] = {};
    for (size_t offset = 0; offset < actual.size(); offset += recordSize)
    {
        const auto index = static_cast<size_t>(actual[offset] - 'A');
        ASSERT_LT(index, producerCount);
        EXPECT_FALSE(seen[index]);
        seen[index] = true;
        EXPECT_EQ(actual.substr(offset, recordSize), std::string(recordSize, actual[offset]));
    }
}

TEST(AsyncWorkerTest, SlowOutputDoesNotHoldProducerMutex)
{
    std::promise<void> entered;
    auto enteredResult = entered.get_future();
    std::promise<void> release;
    auto releaseResult = release.get_future();
    bool firstCall = true;
    std::string actual;
    AsyncWorker worker([&](const char *data, size_t length)
                       {
                           if (firstCall)
                           {
                               firstCall = false;
                               entered.set_value();
                               releaseResult.wait();
                           }
                           actual.append(data, length); },
                       60);
    const std::string fullBlock(kLargeFixedBufferSize, 'S');
    worker.append(fullBlock.data(), fullBlock.size());
    worker.start();
    const auto enteredStatus = enteredResult.wait_for(5s);

    auto producer = std::async(std::launch::async, [&]
                               { worker.append("tail", 4); });
    const auto appendedStatus = producer.wait_for(5s);
    // 无论断言结果如何，先放行输出，避免失败测试自身卡在析构或 join。
    release.set_value();
    producer.get();
    worker.stop();

    EXPECT_EQ(enteredStatus, std::future_status::ready);
    EXPECT_EQ(appendedStatus, std::future_status::ready);
    EXPECT_EQ(actual, fullBlock + "tail");
}

TEST(AsyncWorkerTest, DeliversBytesToStdoutFlush)
{
    const std::string message("stdout\0bytes", 12);
    StdoutFlush output;
    AsyncWorker worker([&](const char *data, size_t length)
                       { output.writeAndFlush(data, length); },
                       60);

    ::testing::internal::CaptureStdout();
    worker.start();
    worker.append(message.data(), message.size());
    worker.stop();
    const auto actual = ::testing::internal::GetCapturedStdout();
    EXPECT_EQ(actual, message);
}

TEST(AsyncWorkerTest, DeliversBytesToPlainFileFlush)
{
    const auto filename = std::filesystem::temp_directory_path() /
                          ("dlog_worker_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    const std::string message("file\0bytes", 10);
    {
        FileFlush output(filename);
        AsyncWorker worker([&](const char *data, size_t length)
                           { output.writeAndFlush(data, length); },
                           60);
        worker.start();
        worker.append(message.data(), message.size());
        worker.stop();
    }

    {
        std::ifstream input(filename, std::ios::binary);
        ASSERT_TRUE(input);
        const std::string actual{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_EQ(actual, message);
    }
    EXPECT_TRUE(std::filesystem::remove(filename));
}

TEST(AsyncWorkerTest, DeliversBatchesToRollingFileFlush)
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("dlog_worker_roll_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        RollFileFlush output(directory / "app", 4);
        AsyncWorker worker([&](const char *data, size_t length)
                           { output.writeAndFlush(data, length); },
                           60);
        worker.append("abcd", 4);
        worker.start();
        worker.stop();
        worker.append("e", 1);
        worker.start();
        worker.stop();
    }

    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    ASSERT_EQ(files.size(), 2u);
    const std::string expected[] = {"abcd", "e"};
    for (size_t index = 0; index < files.size(); ++index)
    {
        std::ifstream input(files[index], std::ios::binary);
        const std::string actual{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        EXPECT_EQ(actual, expected[index]);
    }
    EXPECT_EQ(std::filesystem::remove_all(directory), 3u);
}
