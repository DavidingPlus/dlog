#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "asynclogging.h"


using namespace std::chrono_literals;


namespace
{

    std::filesystem::path makeAsyncLoggingTempDirectory()
    {
        auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("dlog_asynclogging_test_" + std::to_string(suffix));
    }

    std::vector<std::filesystem::path> findAsyncLogFiles(const std::filesystem::path &basePath)
    {
        std::vector<std::filesystem::path> files;
        const auto directory = basePath.has_parent_path() ? basePath.parent_path() : std::filesystem::path(".");
        const std::string prefix = basePath.filename().string() + '.';
        const std::string suffix = ".log";

        for (const auto &entry : std::filesystem::directory_iterator(directory))
        {
            const std::string filename = entry.path().filename().string();
            if (0 == filename.rfind(prefix, 0) &&
                filename.size() >= suffix.size() &&
                0 == filename.compare(filename.size() - suffix.size(), suffix.size(), suffix))
            {
                files.push_back(entry.path());
            }
        }

        // 测试文件通常只有少量序号；先按文件名排序，确保跨文件拼接顺序稳定。
        std::sort(files.begin(), files.end());


        return files;
    }

    std::string readAsyncLogFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("failed to open async log file: " + path.string());

        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::string readAllAsyncLogs(const std::filesystem::path &basePath)
    {
        std::string contents;
        for (const auto &file : findAsyncLogFiles(basePath)) contents += readAsyncLogFile(file);
        return contents;
    }

    bool waitForAsyncLogSize(const std::filesystem::path &basePath, std::uintmax_t expectedSize, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        do
        {
            std::uintmax_t totalSize = 0;
            for (const auto &file : findAsyncLogFiles(basePath))
            {
                std::error_code error;
                const auto fileSize = std::filesystem::file_size(file, error);
                if (!error) totalSize += fileSize;
            }

            if (totalSize >= expectedSize) return true;
            std::this_thread::sleep_for(10ms);
        } while (std::chrono::steady_clock::now() < deadline);


        return false;
    }


    class AsyncLoggingTest : public ::testing::Test
    {

    protected:

        void SetUp() override
        {
            m_directory = makeAsyncLoggingTempDirectory();

            std::error_code error;
            std::filesystem::create_directories(m_directory, error);
            ASSERT_FALSE(error) << error.message();

            m_basePath = m_directory / "async";
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_directory, error);
            EXPECT_FALSE(error) << error.message();
        }


        std::filesystem::path m_directory;

        std::filesystem::path m_basePath;
    };

} // namespace


// 未启动时 stop() 是空操作，重复调用也安全。
TEST_F(AsyncLoggingTest, StopBeforeStartIsSafeAndDoesNotCreateAFile)
{
    AsyncLogging logging(m_basePath.string(), 1024, 60);

    EXPECT_NO_THROW(logging.stop());
    EXPECT_NO_THROW(logging.stop());
    EXPECT_TRUE(findAsyncLogFiles(m_basePath).empty());
}

// 空日志周期可以正常启动、停止；LogFile 会创建空文件，但不写入任何内容。
TEST_F(AsyncLoggingTest, EmptyRunStopsCleanly)
{
    AsyncLogging logging(m_basePath.string(), 1024, 60);

    logging.start();
    logging.stop();
    EXPECT_NO_THROW(logging.stop());

    const auto files = findAsyncLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_TRUE(readAsyncLogFile(files.front()).empty());
}

// 停止时会交出并写入尚未填满的生产者缓冲区。
TEST_F(AsyncLoggingTest, StopFlushesPartialProducerBuffer)
{
    const std::string message = "tail-that-does-not-fill-a-buffer\n";
    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);

    logging.start();
    logging.append(message.data(), message.size());
    logging.stop();

    EXPECT_EQ(readAllAsyncLogs(m_basePath), message);
}

// start() 前追加的尾部数据会在启动后由后台线程处理，并在 stop() 时排空。
TEST_F(AsyncLoggingTest, FlushesDataAppendedBeforeStart)
{
    const std::string message = "buffered-before-start\n";
    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);

    logging.append(message.data(), message.size());
    logging.start();
    logging.stop();

    EXPECT_EQ(readAllAsyncLogs(m_basePath), message);
}

// 空指针和零长度追加都不会写入数据，也不会影响后续正常日志。
TEST_F(AsyncLoggingTest, IgnoresNullAndZeroLengthAppends)
{
    const std::string message = "valid\n";
    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);

    logging.start();
    logging.append(nullptr, 8);
    logging.append(message.data(), 0);
    logging.append(nullptr, 0);
    logging.append(message.data(), message.size());
    logging.stop();

    EXPECT_EQ(readAllAsyncLogs(m_basePath), message);
}

// append() 使用显式长度传递字节，因此消息中间的 '\0' 必须原样保留。
TEST_F(AsyncLoggingTest, PreservesEmbeddedNullBytes)
{
    const char bytes[] = {'A', '\0', 'B', '\n'};
    const std::string expected(bytes, sizeof(bytes));
    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);

    logging.start();
    logging.append(bytes, sizeof(bytes));
    logging.stop();

    EXPECT_EQ(readAllAsyncLogs(m_basePath), expected);
}

// 恰好填满一个缓冲区时不丢字节；下一次追加超过文件轮转阈值后写入新文件。
TEST_F(AsyncLoggingTest, HandlesExactBufferBoundaryAndRollsOnNextAppend)
{
    const std::string fullBuffer(kLargeBufferSize, 'F');
    AsyncLogging logging(m_basePath.string(), static_cast<int64_t>(kLargeBufferSize), 60);

    logging.start();
    logging.append(fullBuffer.data(), fullBuffer.size());
    ASSERT_TRUE(waitForAsyncLogSize(m_basePath, fullBuffer.size(), 10s));

    logging.append("X", 1);
    logging.stop();

    const auto files = findAsyncLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(readAsyncLogFile(files[0]), fullBuffer);
    EXPECT_EQ(readAsyncLogFile(files[1]), "X");
}

// 一次 append 大于两块缓冲区时会分段排队，停止时仍需写完所有整块和最后的尾部。
TEST_F(AsyncLoggingTest, WritesPayloadLargerThanMultipleBuffersWithoutLoss)
{
    const std::string payload(2 * kLargeBufferSize + 137, 'L');
    AsyncLogging logging(m_basePath.string(), static_cast<int64_t>(3 * kLargeBufferSize), 60);

    logging.start();
    logging.append(payload.data(), payload.size());
    logging.stop();

    EXPECT_EQ(readAllAsyncLogs(m_basePath), payload);
}

// flushInterval 为 0 时按实现约定至少等待一秒，尾部数据仍应周期性落到文件层。
TEST_F(AsyncLoggingTest, ZeroFlushIntervalStillFlushesPartialDataPeriodically)
{
    const std::string message = "periodic-flush\n";
    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 0);

    logging.start();
    logging.append(message.data(), message.size());
    const bool becameVisibleBeforeStop = waitForAsyncLogSize(m_basePath, message.size(), 8s);

    logging.stop();

    EXPECT_TRUE(becameVisibleBeforeStop);
    EXPECT_EQ(readAllAsyncLogs(m_basePath), message);
}

// 运行中的重复 start() 不会重复启动消费者；重复 stop() 不会重复 join。
TEST_F(AsyncLoggingTest, RepeatedStartAndStopAreSafe)
{
    const std::string message = "written-once\n";
    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);

    logging.start();
    logging.start();
    logging.append(message.data(), message.size());
    logging.stop();
    EXPECT_NO_THROW(logging.stop());

    EXPECT_EQ(readAllAsyncLogs(m_basePath), message);
    EXPECT_EQ(findAsyncLogFiles(m_basePath).size(), 1u);
}

// stop() 完成上一轮写盘后，同一个 AsyncLogging 对象可以重新启动并继续写日志。
TEST_F(AsyncLoggingTest, CanRestartAfterStop)
{
    const std::string first = "first-run\n";
    const std::string second = "second-run\n";
    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);

    logging.start();
    logging.append(first.data(), first.size());
    logging.stop();

    logging.start();
    logging.append(second.data(), second.size());
    logging.stop();

    EXPECT_EQ(readAllAsyncLogs(m_basePath), first + second);
    EXPECT_EQ(findAsyncLogFiles(m_basePath).size(), 2u);
}

// 析构函数会 stop() 并等待后台线程刷新尾部数据。
TEST_F(AsyncLoggingTest, DestructorStopsWorkerAndFlushesTail)
{
    const std::string message = "flushed-by-destructor\n";

    {
        AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);
        logging.start();
        logging.append(message.data(), message.size());
    }

    EXPECT_EQ(readAllAsyncLogs(m_basePath), message);
}

// 多个生产者并发追加时，每次 append 的记录不交错、不丢失、不重复。
TEST_F(AsyncLoggingTest, ConcurrentProducersPreserveEveryWholeRecord)
{
    constexpr int producerCount = 4;
    constexpr int recordsPerProducer = 100;

    AsyncLogging logging(m_basePath.string(), 1024 * 1024, 60);
    logging.start();

    std::vector<std::thread> producers;
    std::unordered_set<std::string> expectedRecords;
    for (int producer = 0; producer < producerCount; ++producer)
    {
        producers.emplace_back([producer, recordsPerProducer, &logging]()
                               {
                                   for (int record = 0; record < recordsPerProducer; ++record)
                                   {
                                       const std::string message = "producer-" + std::to_string(producer) + "-record-" + std::to_string(record) + '\n';
                                       logging.append(message.data(), message.size());
                                   } //
                               });

        for (int record = 0; record < recordsPerProducer; ++record) expectedRecords.insert("producer-" + std::to_string(producer) + "-record-" + std::to_string(record));
    }

    for (auto &producer : producers) producer.join();
    logging.stop();

    std::istringstream input(readAllAsyncLogs(m_basePath));
    std::unordered_set<std::string> actualRecords;
    std::string line;
    while (std::getline(input, line)) EXPECT_TRUE(actualRecords.insert(line).second) << "duplicate record: " << line;

    ASSERT_EQ(actualRecords.size(), expectedRecords.size());
    for (const auto &record : expectedRecords) EXPECT_EQ(actualRecords.count(record), 1u) << "missing record: " << record;
}
