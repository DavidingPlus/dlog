#include <gtest/gtest.h>

#include "threadpool.h"

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>


using namespace dlog;
using namespace std::chrono_literals;


TEST(ThreadPoolTest, RejectsZeroThreads)
{
    EXPECT_THROW({ ThreadPool pool(0); }, std::invalid_argument);
}

TEST(ThreadPoolTest, ExecutesTasksAndReturnsValuesAndExceptions)
{
    ThreadPool pool(2);
    auto sum = pool.enqueue([](int a, int b) { return a + b; }, 2, 3);
    auto failure = pool.enqueue([]() -> int { throw std::runtime_error("task failed"); });
    auto next = pool.enqueue([] { return 7; });

    EXPECT_EQ(sum.get(), 5);
    EXPECT_THROW(failure.get(), std::runtime_error);
    EXPECT_EQ(next.get(), 7);
}

TEST(ThreadPoolTest, RunsTasksOnProjectThreadsAndReusesThem)
{
    const int before = Thread::NumCreated();
    ThreadPool pool(1);
    const auto caller = std::this_thread::get_id();
    auto first = pool.enqueue([] { return std::this_thread::get_id(); });
    auto second = pool.enqueue([] { return std::this_thread::get_id(); });

    EXPECT_EQ(Thread::NumCreated(), before + 1);
    const auto worker = first.get();
    EXPECT_NE(worker, caller);
    EXPECT_EQ(second.get(), worker);
}

TEST(ThreadPoolTest, MultipleWorkersExecuteTasksConcurrently)
{
    ThreadPool pool(2);
    std::promise<void> release;
    auto ready = release.get_future().share();
    std::promise<void> firstStarted;
    std::promise<void> secondStarted;
    auto firstSignal = firstStarted.get_future();
    auto secondSignal = secondStarted.get_future();

    auto first = pool.enqueue([&] { firstStarted.set_value(); ready.wait(); });
    auto second = pool.enqueue([&] { secondStarted.set_value(); ready.wait(); });
    const auto firstStatus = firstSignal.wait_for(5s);
    const auto secondStatus = secondSignal.wait_for(5s);
    release.set_value();

    EXPECT_EQ(firstStatus, std::future_status::ready);
    EXPECT_EQ(secondStatus, std::future_status::ready);
    first.get();
    second.get();
}

TEST(ThreadPoolTest, DestructorDrainsQueuedTasks)
{
    std::atomic<int> completed{0};
    std::vector<std::future<void>> results;
    {
        ThreadPool pool(1);
        for (int i = 0; i < 20; ++i)
            results.emplace_back(pool.enqueue([&completed] { ++completed; }));
    }

    EXPECT_EQ(completed.load(), 20);
    for (auto &result : results) EXPECT_NO_THROW(result.get());
}

TEST(ThreadPoolTest, ConcurrentProducersSubmitAllTasks)
{
    std::atomic<int> completed{0};
    {
        ThreadPool pool(3);
        std::vector<std::thread> producers;
        for (int i = 0; i < 4; ++i)
        {
            producers.emplace_back([&] {
                for (int j = 0; j < 50; ++j) pool.enqueue([&completed] { ++completed; });
            });
        }
        for (auto &producer : producers) producer.join();
    }

    EXPECT_EQ(completed.load(), 200);
}

TEST(ThreadPoolTest, DestructorWaitsForRunningAndQueuedTasks)
{
    auto pool = std::make_unique<ThreadPool>(1);
    std::promise<void> started;
    auto startedSignal = started.get_future();
    std::promise<void> release;
    auto releaseSignal = release.get_future().share();
    std::atomic<bool> queuedCompleted{false};
    auto first = pool->enqueue([&] { started.set_value(); releaseSignal.wait(); });
    auto second = pool->enqueue([&] { queuedCompleted = true; });
    const auto startedStatus = startedSignal.wait_for(5s);

    std::promise<void> destroying;
    auto destroyingSignal = destroying.get_future();
    auto destruction = std::async(std::launch::async, [pool = std::move(pool), &destroying]() mutable {
        destroying.set_value();
        pool.reset();
    });
    const auto destroyingStatus = destroyingSignal.wait_for(5s);
    const auto earlyStatus = destruction.wait_for(50ms);
    release.set_value();

    EXPECT_EQ(startedStatus, std::future_status::ready);
    EXPECT_EQ(destroyingStatus, std::future_status::ready);
    EXPECT_EQ(earlyStatus, std::future_status::timeout);
    EXPECT_EQ(destruction.wait_for(5s), std::future_status::ready);
    destruction.get();
    EXPECT_NO_THROW(first.get());
    EXPECT_NO_THROW(second.get());
    EXPECT_TRUE(queuedCompleted.load());
}
