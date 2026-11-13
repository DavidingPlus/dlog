#include <gtest/gtest.h>

#include "logworker.h"
#include "fixedbuffer.h"
#include "threadpool.h"
#include "logflush.h"
#include "logstream.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>


using namespace dlog;
using namespace std::chrono_literals;

namespace
{
class TestFlush : public LogFlush
{
public:
    void flush() override {}
    explicit TestFlush(std::function<void(const char *, size_t)> output) : m_output(std::move(output)) {}
    void write(const char *data, size_t len) override { m_output(data, len); }
private:
    std::function<void(const char *, size_t)> m_output;
};

auto makeTestFlush(std::function<void(const char *, size_t)> output)
{
    return std::make_shared<TestFlush>(std::move(output));
}
} // namespace


TEST(LogWorkerTest, RejectsEmptyOutput)
{
    EXPECT_THROW({ LogWorker worker(std::shared_ptr<LogFlush>{}, std::make_shared<ThreadPool>(1)); }, std::invalid_argument);
}

TEST(LogWorkerTest, FlushesEachBatchAndReportsFlushFailure)
{
    class FlushProbe : public LogFlush
    {
    public:
        void write(const char *data, size_t len) override { events.append(data, len); events += '|'; }
        void flush() override
        {
            events += "flush|";
            if (++flushes == 2) throw std::runtime_error("flush failed");
        }
        std::string events;
        int flushes = 0;
    };
    auto logFlush = std::make_shared<FlushProbe>();
    LogWorker worker(logFlush, std::make_shared<ThreadPool>(1));
    worker.append("first", 5);
    worker.start();
    worker.stop();
    EXPECT_EQ(logFlush->events, "first|flush|");
    worker.append("second", 6);
    worker.start();
    EXPECT_THROW(worker.stop(), std::runtime_error);
    EXPECT_EQ(logFlush->events, "first|flush|second|flush|");
    EXPECT_THROW(worker.append("later", 5), std::runtime_error);
}

TEST(LogWorkerTest, CopiesStreamBeforeItIsDestroyedAndOwnsOutput)
{
    std::string actual;
    auto logFlush = makeTestFlush([&](const char *data, size_t len) { actual.append(data, len); });
    std::weak_ptr<LogFlush> observer = logFlush;
    {
        LogWorker worker(logFlush, std::make_shared<ThreadPool>(1));
        logFlush.reset();
        {
            LogStream stream;
            stream.append("a\0b", 3);
            worker.append(stream);
        }
        EXPECT_FALSE(observer.expired());
        worker.start();
        worker.stop();
    }
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(actual, std::string("a\0b", 3));
}

TEST(LogWorkerTest, CopiesLargeBinaryDataAndCallsOutputOnBackgroundThread)
{
    std::string input(2 * kLargeFixedBufferSize + 137, 'W');
    input[kLargeFixedBufferSize] = '\0';
    const auto expected = input;
    std::string actual;
    bool allCallsOnBackgroundThread = true;
    size_t calls = 0;
    const auto caller = std::this_thread::get_id();
    LogWorker worker(makeTestFlush([&](const char *data, size_t len)
                       {
                           ++calls;
                           allCallsOnBackgroundThread &= std::this_thread::get_id() != caller;
                           actual.append(data, len); }),
                       std::make_shared<ThreadPool>(2));

    worker.append(input.data(), input.size());
    input.assign(input.size(), 'X');
    EXPECT_TRUE(actual.empty());
    worker.start();
    worker.stop();

    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(allCallsOnBackgroundThread);
    EXPECT_EQ(calls, 1u);
}

TEST(LogWorkerTest, RepeatedStartStopAndAppendBetweenRunsPreserveData)
{
    std::string actual;
    LogWorker worker(makeTestFlush([&](const char *data, size_t len)
                       { actual.append(data, len); }),
                       std::make_shared<ThreadPool>(2));

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

TEST(LogWorkerTest, EmptyWritesDoNotInvokeOutput)
{
    size_t calls = 0;
    LogWorker worker(makeTestFlush([&](const char *, size_t)
                       { ++calls; }),
                       std::make_shared<ThreadPool>(2));
    worker.append(nullptr, 1);
    worker.append(nullptr, 0);
    worker.append("data", 0);
    worker.start();
    worker.stop();
    EXPECT_EQ(calls, 0u);
}

TEST(LogWorkerTest, DestructorDrainsPartialBlock)
{
    const std::string message("tail\0bytes", 10);
    std::string actual;
    {
        LogWorker worker(makeTestFlush([&](const char *data, size_t len)
                           { actual.append(data, len); }),
                       std::make_shared<ThreadPool>(2));
        worker.append(message.data(), message.size());
        worker.start();
    }
    EXPECT_EQ(actual, message);
}

TEST(LogWorkerTest, StopRethrowsOriginalBackgroundError)
{
    LogWorker worker(makeTestFlush([](const char *, size_t)
                       { throw std::runtime_error("output failed"); }),
                       std::make_shared<ThreadPool>(2));
    worker.append("tail", 4);
    worker.start();

    try
    {
        worker.stop();
        ADD_FAILURE() << "Expected the background error";
    }
    catch (const std::runtime_error &error)
    {
        EXPECT_STREQ(error.what(), "output failed");
    }

    EXPECT_THROW(worker.append("more", 4), std::runtime_error);
    EXPECT_THROW(worker.start(), std::runtime_error);
    EXPECT_THROW(worker.stop(), std::runtime_error);
}

TEST(LogWorkerTest, JoinsAfterBackgroundFailureBeforeStop)
{
    struct OutputError
    {
        int code;
    };
    std::promise<void> entered;
    auto enteredResult = entered.get_future();
    std::promise<void> release;
    auto releaseResult = release.get_future();
    size_t calls = 0;
    LogWorker worker(makeTestFlush([&](const char *, size_t)
                       {
                           ++calls;
                           entered.set_value();
                           releaseResult.wait();
                           throw OutputError{71}; }),
                       std::make_shared<ThreadPool>(2));
    const std::string fullBlock(kLargeFixedBufferSize, 'E');
    worker.append(fullBlock.data(), fullBlock.size());
    worker.start();
    const auto enteredStatus = enteredResult.wait_for(5s);
    if (enteredStatus != std::future_status::ready)
    {
        release.set_value();
        FAIL() << "Expected output before stop";
    }

    // 在回调抛错前留下待写数据，验证失败后不会继续输出它们。
    worker.append(fullBlock.data(), fullBlock.size());
    worker.append("tail", 4);
    release.set_value();

    bool failedBeforeStop = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        try
        {
            worker.append("x", 1);
        }
        catch (const OutputError &error)
        {
            EXPECT_EQ(error.code, 71);
            failedBeforeStop = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }

    EXPECT_TRUE(failedBeforeStop);
    EXPECT_THROW(worker.stop(), OutputError);
    EXPECT_THROW(worker.stop(), OutputError);
    EXPECT_THROW(worker.start(), OutputError);
    EXPECT_EQ(calls, 1u);
}

TEST(LogWorkerTest, DestructorJoinsAfterBackgroundErrorWithoutExplicitStop)
{
    size_t calls = 0;
    {
        LogWorker worker(makeTestFlush([&](const char *, size_t)
                           {
                               ++calls;
                               throw std::runtime_error("output failed during destruction"); }),
                       std::make_shared<ThreadPool>(2));
        worker.append("tail", 4);
        worker.start();
    }
    EXPECT_EQ(calls, 1u);
}

TEST(LogWorkerTest, PartialDataIsProcessedWithoutWaitingForStop)
{
    std::promise<std::string> delivered;
    auto result = delivered.get_future();
    LogWorker worker(makeTestFlush([&](const char *data, size_t len)
                       { delivered.set_value(std::string(data, len)); }),
                       std::make_shared<ThreadPool>(2));
    worker.append("promptly", 8);
    worker.start();
    const auto status = result.wait_for(5s);
    worker.stop();

    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(result.get(), "promptly");
}

TEST(LogWorkerTest, ConcurrentLongRecordsStayWhole)
{
    constexpr size_t recordSize = kLargeFixedBufferSize + 31;
    constexpr size_t producerCount = 4;
    std::string actual;
    LogWorker worker(makeTestFlush([&](const char *data, size_t len)
                       { actual.append(data, len); }),
                       std::make_shared<ThreadPool>(2));
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

TEST(LogWorkerTest, SlowOutputDoesNotHoldProducerMutex)
{
    std::promise<void> entered;
    auto enteredResult = entered.get_future();
    std::promise<void> release;
    auto releaseResult = release.get_future();
    bool firstCall = true;
    std::string actual;
    LogWorker worker(makeTestFlush([&](const char *data, size_t len)
                       {
                           if (firstCall)
                           {
                               firstCall = false;
                               entered.set_value();
                               releaseResult.wait();
                           }
                           actual.append(data, len); }),
                       std::make_shared<ThreadPool>(2));
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

TEST(LogWorkerTest, TwoWorkersUseSharedPoolConcurrently)
{
    auto pool = std::make_shared<ThreadPool>(2);
    std::promise<void> firstEntered, secondEntered, release;
    auto firstReady = firstEntered.get_future();
    auto secondReady = secondEntered.get_future();
    auto releaseSignal = release.get_future().share();
    LogWorker first(makeTestFlush([&](const char *, size_t)
                    { firstEntered.set_value(); releaseSignal.wait(); }), pool);
    LogWorker second(makeTestFlush([&](const char *, size_t)
                     { secondEntered.set_value(); releaseSignal.wait(); }), pool);
    first.start();
    second.start();
    first.append("a", 1);
    second.append("b", 1);
    const auto firstStatus = firstReady.wait_for(5s);
    const auto secondStatus = secondReady.wait_for(5s);
    release.set_value();
    first.stop();
    second.stop();
    EXPECT_EQ(firstStatus, std::future_status::ready);
    EXPECT_EQ(secondStatus, std::future_status::ready);
}

TEST(LogWorkerTest, ReusesTwoBuffersWhileOutputIsBlocked)
{
    std::promise<void> firstEntered, secondEntered, releaseFirst, releaseSecond;
    auto firstReady = firstEntered.get_future();
    auto secondReady = secondEntered.get_future();
    auto firstRelease = releaseFirst.get_future();
    auto secondRelease = releaseSecond.get_future();
    std::vector<const char *> buffers;
    std::string actual;
    LogWorker worker(makeTestFlush([&](const char *data, size_t len)
                     {
                         buffers.push_back(data);
                         actual.append(data, len);
                         if (buffers.size() == 1)
                         {
                             firstEntered.set_value();
                             firstRelease.wait();
                         }
                         else if (buffers.size() == 2)
                         {
                             secondEntered.set_value();
                             secondRelease.wait();
                         }
                     }), std::make_shared<ThreadPool>(2));
    worker.start();
    worker.append("first", 5);
    const auto firstStatus = firstReady.wait_for(5s);
    if (firstStatus != std::future_status::ready)
    {
        releaseFirst.set_value();
        releaseSecond.set_value();
        worker.stop();
        FAIL() << "First output did not start";
    }
    worker.append("second", 6);
    releaseFirst.set_value();
    const auto secondStatus = secondReady.wait_for(5s);
    if (secondStatus != std::future_status::ready)
    {
        releaseSecond.set_value();
        worker.stop();
        FAIL() << "Second output did not start";
    }
    worker.append("third", 5);
    releaseSecond.set_value();
    worker.stop();

    ASSERT_EQ(buffers.size(), 3u);
    EXPECT_NE(buffers[0], buffers[1]);
    EXPECT_EQ(buffers[0], buffers[2]);
    EXPECT_EQ(actual, "firstsecondthird");
}

TEST(LogWorkerTest, DeliversBytesToStdoutFlush)
{
    const std::string message("stdout\0bytes", 12);
    auto logFlush = std::make_shared<StdoutFlush>();
    LogWorker worker(logFlush,
                       std::make_shared<ThreadPool>(2));

    ::testing::internal::CaptureStdout();
    worker.start();
    worker.append(message.data(), message.size());
    worker.stop();
    const auto actual = ::testing::internal::GetCapturedStdout();
    EXPECT_EQ(actual, message);
}

TEST(LogWorkerTest, DeliversBytesToPlainFileFlush)
{
    const auto filename = std::filesystem::temp_directory_path() /
                          ("dlog_worker_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    const std::string message("file\0bytes", 10);
    {
        auto logFlush = std::make_shared<FileFlush>(filename);
        LogWorker worker(logFlush,
                       std::make_shared<ThreadPool>(2));
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

#if defined(DLOG_OS_LINUX)
TEST(LogWorkerTest, ReportsFileShortWriteAtStop)
{
    if (!std::filesystem::exists("/dev/full")) GTEST_SKIP() << "/dev/full is unavailable";

    auto logFlush = std::make_shared<FileFlush>("/dev/full");
    LogWorker worker(logFlush,
                       std::make_shared<ThreadPool>(2));
    // 超过文件流的 64 KiB 缓冲，使错误在 append 而非 flush 时发生。
    const std::string batch(128 * 1024, 'x');
    worker.append(batch.data(), batch.size());
    worker.start();

    try
    {
        worker.stop();
        ADD_FAILURE() << "Expected a file write failure";
    }
    catch (const std::system_error &error)
    {
        EXPECT_EQ(error.code(), std::errc::no_space_on_device);
    }
}
#endif

TEST(LogWorkerTest, DeliversBatchesToRollingFileFlush)
{
    const auto directory = std::filesystem::temp_directory_path() /
                           ("dlog_worker_roll_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        auto logFlush = std::make_shared<RollFileFlush>(directory / "app", 4);
        LogWorker worker(logFlush,
                       std::make_shared<ThreadPool>(2));
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
