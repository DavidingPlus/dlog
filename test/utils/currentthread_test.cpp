#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <tuple>

#include "currentthread.h"


// 这个 fixture 专门负责在每个用例前后清空 thread_local 缓存，避免测试互相污染。
class CurrentThreadTest : public testing::Test
{

protected:

    void SetUp() override
    {
        // t_cachedTid 是当前测试线程上的 thread_local 缓存，Google Test 在主线程上跑这个测试，前一个用例调用 tid()/cacheTid() 后留下的值不会自动恢复。这里显式清零，保证每个测试都从“尚未缓存 tid”开始。
        CurrentThread::t_cachedTid = 0;
    }

    void TearDown() override
    {
        // 测试结束后也恢复，避免当前用例的缓存状态泄漏到后续用例。
        CurrentThread::t_cachedTid = 0;
    }
};


// 验证 tid() 会返回当前线程的 kernel tid，并把结果写进缓存。
TEST_F(CurrentThreadTest, TidReturnsKernelThreadId)
{
    const int tid = CurrentThread::tid();

    EXPECT_EQ(tid, static_cast<int>(::gettid()));
    EXPECT_EQ(CurrentThread::t_cachedTid, tid);
}

// 验证第一次调用 tid() 会自动初始化 thread_local 缓存。
TEST_F(CurrentThreadTest, TidPopulatesCacheOnFirstCall)
{
    ASSERT_EQ(CurrentThread::t_cachedTid, 0);

    const int first = CurrentThread::tid();

    EXPECT_NE(first, 0);
    EXPECT_EQ(CurrentThread::t_cachedTid, first);
}

// 验证后续调用 tid() 会直接读取缓存，而不是重新取系统 tid。
TEST_F(CurrentThreadTest, TidReturnsCachedValueOnSubsequentCalls)
{
    const int first = CurrentThread::tid();
    ASSERT_NE(first, 0);

    CurrentThread::t_cachedTid = 123456;

    const int second = CurrentThread::tid();

    EXPECT_EQ(second, 123456);
}

// 验证 cacheTid() 可以手动把当前线程 tid 写入缓存。
TEST_F(CurrentThreadTest, CacheTidInitializesCacheExplicitly)
{
    ASSERT_EQ(CurrentThread::t_cachedTid, 0);

    CurrentThread::cacheTid();

    EXPECT_EQ(CurrentThread::t_cachedTid, static_cast<int>(::gettid()));
    EXPECT_EQ(CurrentThread::tid(), CurrentThread::t_cachedTid);
}

// 验证缓存已经有值时，cacheTid() 不会覆盖它。
TEST_F(CurrentThreadTest, CacheTidDoesNotOverwriteExistingCache)
{
    CurrentThread::t_cachedTid = 424242;

    CurrentThread::cacheTid();

    EXPECT_EQ(CurrentThread::t_cachedTid, 424242);
}

// 验证不同线程各自维护独立的 tid 缓存。
TEST_F(CurrentThreadTest, DifferentThreadsKeepIndependentCaches)
{
    const int mainTid = CurrentThread::tid();
    ASSERT_NE(mainTid, 0);

    std::promise<std::tuple<int, int, int>> promise;
    auto future = promise.get_future();

    // 子线程先读一次缓存，再连续两次调用 tid()，观察缓存是否稳定。
    std::thread worker([&promise]()
                       {
                           const int initialCache = CurrentThread::t_cachedTid;
                           const int firstTid = CurrentThread::tid();
                           const int secondTid = CurrentThread::tid();
                           promise.set_value({initialCache, firstTid, secondTid}); });

    const auto [initialCache, workerTid, workerTidAgain] = future.get();
    worker.join();

    EXPECT_EQ(initialCache, 0);
    EXPECT_NE(workerTid, 0);
    EXPECT_EQ(workerTidAgain, workerTid);
    EXPECT_NE(workerTid, mainTid);
    EXPECT_EQ(CurrentThread::tid(), mainTid);
    EXPECT_EQ(CurrentThread::t_cachedTid, mainTid);
}
