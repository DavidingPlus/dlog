#include "skiplist.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>


constexpr int kThreadCount = 1;
// 插入和查询两个阶段各执行 kTestCount 次操作。
constexpr int kTestCount = 100000;

static_assert(kThreadCount > 0, "Thread count must be positive.");
static_assert(kTestCount > 0, "Test count must be positive.");

// 全部工作线程访问同一个跳表。
SkipList<int, std::string> skipList(18);


int getOperationCount(int threadId)
{
    // 平均分配操作次数，余数分给前几个线程，确保合计恰好为 kTestCount。
    return kTestCount / kThreadCount + (threadId < kTestCount % kThreadCount ? 1 : 0);
}

void insertElement(int threadId, unsigned int seed)
{
    // 每个线程有自己的随机数引擎，不共享测试 key 的随机状态。uniform_int_distribution 覆盖 [0, kTestCount - 1]，不受 RAND_MAX 限制。
    std::mt19937 randomEngine(seed + static_cast<unsigned int>(threadId));
    std::uniform_int_distribution<int> randomKey(0, kTestCount - 1);

    int operationCount = getOperationCount(threadId);
    for (int count = 0; count < operationCount; ++count)
    {
        // 随机 key 可能重复：这里统计的是插入尝试次数，不是新增节点数量。
        skipList.insertElement(randomKey(randomEngine), "a");
    }
}

void getElement(int threadId, unsigned int seed)
{
    std::mt19937 randomEngine(seed + static_cast<unsigned int>(threadId));
    std::uniform_int_distribution<int> randomKey(0, kTestCount - 1);

    int operationCount = getOperationCount(threadId);
    for (int count = 0; count < operationCount; ++count)
    {
        // 查询随机 key，既可能命中已有节点，也可能查不到。
        skipList.searchElement(randomKey(randomEngine));
    }
}

// name 是测试阶段名称；operation 接收线程编号和随机种子，执行该线程的测试任务。std::function 可以保存普通函数、lambda 或函数对象，要求调用形式为 void(int, unsigned int)。
void runTest(const std::string &name, const std::function<void(int, unsigned int)> &operation, unsigned int seed)
{
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    // steady_clock 单调递增，适合测量耗时，不受系统时间调整影响。
    auto start = std::chrono::steady_clock::now();

    try
    {
        for (int threadId = 0; threadId < kThreadCount; ++threadId)
        {
            std::cout << "main(): creating thread " << threadId << std::endl;
            // 参数直接按值传入，无需将线程编号转换成 void*。
            threads.emplace_back(operation, threadId, seed);
        }
    }
    catch (...)
    {
        // 如果创建后续线程失败，先等待已创建的线程，避免销毁可 join 的线程导致 terminate。
        for (std::thread &thread : threads) thread.join();
        throw;
    }

    // join() 等待全部任务完成；线程函数正常返回即可，不需要 pthread_exit()。
    for (std::thread &thread : threads) thread.join();

    auto finish = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed = finish - start;
    std::cout << name << " elapsed: " << elapsed.count() << " seconds" << std::endl;

    // 耗时包括线程创建、随机 key 生成、跳表操作及其日志输出。
}


int main()
{
    unsigned int seed = static_cast<unsigned int>(std::time(nullptr));
    // 跳表内部使用 std::rand() 的播种方式；测试 key 使用上面的独立 C++ 随机引擎。
    std::srand(seed);

    try
    {
        runTest("insert", insertElement, seed);

        std::cout << "skipList size: " << skipList.size() << std::endl;

        // 插入阶段的所有线程结束后，再执行查询测试；换一个 seed 生成查询序列。
        runTest("get", getElement, seed + 1U);
    }
    catch (std::exception &error)
    {
        std::cerr << "Stress test failed: " << error.what() << std::endl;
        return EXIT_FAILURE;
    }

    // 正常结束程序时析构全局跳表；此时所有工作线程均已 join。
}
