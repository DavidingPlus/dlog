#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include "thread.h"

#include "currentthread.h"


using namespace std::chrono_literals;


// 验证默认构造会生成线程名，并递增已创建线程计数。
TEST(ThreadTest, ConstructorAssignsDefaultNameAndIncrementsCounter)
{
    const int before = Thread::NumCreated();

    Thread thread([]() {});

    EXPECT_EQ(Thread::NumCreated(), before + 1);
    EXPECT_EQ(thread.name(), "Thread" + std::to_string(before + 1));
    EXPECT_FALSE(thread.started());
    EXPECT_EQ(thread.tid(), 0);
}

// 验证显式传入的线程名会被保留。
TEST(ThreadTest, ConstructorPreservesExplicitName)
{
    Thread thread([]() {}, "worker");

    EXPECT_EQ(thread.name(), "worker");
    EXPECT_FALSE(thread.started());
    EXPECT_EQ(thread.tid(), 0);
}

// 验证 start() 会启动线程、执行回调，并写回 kernel tid。
TEST(ThreadTest, StartRunsCallbackAndPublishesKernelTid)
{
    std::promise<int> promise;
    auto future = promise.get_future();

    Thread thread([&promise]()
                  { promise.set_value(CurrentThread::tid()); },
                  "starter");

    thread.start();

    EXPECT_TRUE(thread.started());
    EXPECT_NE(thread.tid(), 0);
    EXPECT_EQ(future.get(), thread.tid());

    thread.join();
    EXPECT_FALSE(thread.started());
}

// 验证同一个 Thread 对象在上一轮 join 后可以再次启动并执行回调。
TEST(ThreadTest, CanRestartAfterJoin)
{
    std::atomic<int> callbackCount{0};
    Thread thread([&callbackCount]()
                  { ++callbackCount; });

    EXPECT_FALSE(thread.started());

    thread.start();
    EXPECT_TRUE(thread.started());
    thread.join();
    EXPECT_FALSE(thread.started());
    EXPECT_EQ(callbackCount.load(), 1);

    thread.start();
    EXPECT_TRUE(thread.started());
    thread.join();
    EXPECT_FALSE(thread.started());
    EXPECT_EQ(callbackCount.load(), 2);
}

// 验证当前一轮线程尚未 join 时重复 start 不会再创建一条工作线程。
TEST(ThreadTest, RepeatedStartBeforeJoinDoesNotStartAnotherCallback)
{
    std::atomic<int> callbackCount{0};
    std::promise<void> callbackFinished;
    auto callbackFinishedFuture = callbackFinished.get_future();

    Thread thread([&callbackCount, &callbackFinished]()
                  {
                      if (0 == callbackCount.fetch_add(1)) callbackFinished.set_value(); });

    thread.start();
    callbackFinishedFuture.wait();
    EXPECT_TRUE(thread.started());

    // 回调即使已经执行完，std::thread 在 join 前仍是 joinable，因此此调用应被忽略。
    thread.start();
    thread.join();

    EXPECT_EQ(callbackCount.load(), 1);
    EXPECT_FALSE(thread.started());
}

// 验证未启动时 join 和重复 join 都安全返回。
TEST(ThreadTest, JoinBeforeStartAndRepeatedJoinAreNoOps)
{
    Thread thread([]() {});

    EXPECT_NO_THROW(thread.join());
    EXPECT_FALSE(thread.started());

    thread.start();
    EXPECT_NO_THROW(thread.join());
    EXPECT_FALSE(thread.started());
    EXPECT_NO_THROW(thread.join());
}

// 验证 join() 会等待线程回调执行完毕。
TEST(ThreadTest, JoinWaitsForCallbackCompletion)
{
    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::atomic<bool> finished = false;

    Thread thread([&started, &finished]()
                  {
                      started.set_value();
                      std::this_thread::sleep_for(20ms);
                      finished = true; });

    thread.start();
    startedFuture.wait();

    EXPECT_FALSE(finished.load());

    thread.join();

    EXPECT_TRUE(finished.load());
    EXPECT_FALSE(thread.started());
}

// 验证析构时未 join 的线程会被分离，但仍会继续运行结束。
TEST(ThreadTest, DestructorDetachesRunningThread)
{
    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::promise<void> finished;
    auto finishedFuture = finished.get_future();

    {
        Thread thread([&started, &finished]()
                      {
                          started.set_value();
                          std::this_thread::sleep_for(20ms);
                          finished.set_value(); });

        thread.start();
        startedFuture.wait();
    }

    EXPECT_EQ(finishedFuture.wait_for(200ms), std::future_status::ready);
}
