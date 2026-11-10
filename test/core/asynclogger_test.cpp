#include <gtest/gtest.h>

#include "asynclogger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


using namespace dlog;


namespace
{

    class AsyncLoggerBuilderTest : public ::testing::Test
    {
    protected:

        void SetUp() override
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            m_directory = std::filesystem::temp_directory_path() / ("dlog_asynclogger_test_" + std::to_string(suffix));
            std::filesystem::create_directories(m_directory);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_directory, error);
            EXPECT_FALSE(error) << error.message();
        }

        std::filesystem::path m_directory;
    };

    class TrackedFlush : public LogFlush
    {
    public:

        TrackedFlush(int &constructions, int &destructions, std::unique_ptr<int> value)
            : m_destructions(destructions), m_value(std::move(value)) { ++constructions; }
        ~TrackedFlush() override { ++m_destructions; }
        void writeAndFlush(const char *, size_t) override {}

    private:

        int &m_destructions;
        std::unique_ptr<int> m_value;
    };

} // namespace


TEST(AsyncLoggerBuilderLifetimeTest, ForwardsArgumentsAndKeepsOutputUntilBuilderIsDestroyed)
{
    int constructions = 0;
    int destructions = 0;
    auto value = std::make_unique<int>(42);
    {
        AsyncLoggerBuilder builder;
        builder.BuildLoggerFlush<TrackedFlush>(constructions, destructions, std::move(value));
        EXPECT_EQ(value, nullptr);
        EXPECT_EQ(constructions, 1);
        EXPECT_EQ(destructions, 0);
    }
    EXPECT_EQ(destructions, 1);
}

TEST_F(AsyncLoggerBuilderTest, CreatesBothFileModesWithNativePaths)
{
    const auto filename = m_directory / "normal" / std::filesystem::u8path(u8"\u65e5\u5fd7 \U0001f680.log");
    const auto basePath = m_directory / "rolling" / std::filesystem::u8path(u8"\u8f6e\u8f6c \U0001f680");
    AsyncLoggerBuilder normal;
    normal.BuildLoggerFlush<FileFlush>(filename);
    AsyncLoggerBuilder rolling;
    rolling.BuildLoggerFlush<RollFileFlush>(basePath, 1024);
    auto normalLogger = normal.Build();
    auto rollingLogger = rolling.Build();
    ASSERT_NE(normalLogger, nullptr);
    ASSERT_NE(rollingLogger, nullptr);
    EXPECT_NO_THROW(normalLogger->stop());
    EXPECT_NO_THROW(rollingLogger->stop());

    ASSERT_TRUE(std::filesystem::is_regular_file(filename));
    EXPECT_EQ(std::filesystem::file_size(filename), 0u);
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(basePath.parent_path())) paths.push_back(entry.path());
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths.front().filename().u8string().find(basePath.filename().u8string() + '.'), 0u);
    EXPECT_EQ(paths.front().extension(), ".log");
    EXPECT_EQ(std::filesystem::file_size(paths.front()), 0u);
}

TEST_F(AsyncLoggerBuilderTest, RejectsSecondOutputBeforeCreatingItsFile)
{
    const auto first = m_directory / "first.log";
    const auto second = m_directory / "rejected" / "second.log";
    AsyncLoggerBuilder builder;
    builder.BuildLoggerFlush<FileFlush>(first);

    EXPECT_THROW(builder.BuildLoggerFlush<FileFlush>(second), std::logic_error);
    EXPECT_TRUE(std::filesystem::is_regular_file(first));
    EXPECT_FALSE(std::filesystem::exists(second.parent_path()));
}

TEST_F(AsyncLoggerBuilderTest, ConstructionErrorsPreserveExistingFileAndAllowRetry)
{
    const auto blocked = m_directory / "blocked";
    {
        std::ofstream output(blocked, std::ios::binary);
        ASSERT_TRUE(output);
        output << "keep";
    }
    const auto invalidPath = blocked / "nested" / "app";
    const auto recovered = m_directory / "recovered.log";
    AsyncLoggerBuilder builder;

    EXPECT_THROW(builder.BuildLoggerFlush<FileFlush>(std::filesystem::path{}), std::invalid_argument);
    EXPECT_THROW(builder.BuildLoggerFlush<RollFileFlush>(std::filesystem::path{}, 1024), std::invalid_argument);
    EXPECT_THROW(builder.BuildLoggerFlush<FileFlush>(invalidPath), std::filesystem::filesystem_error);
    EXPECT_THROW(builder.BuildLoggerFlush<RollFileFlush>(invalidPath, 1024), std::filesystem::filesystem_error);
    EXPECT_NO_THROW(builder.BuildLoggerFlush<FileFlush>(recovered));
    EXPECT_TRUE(std::filesystem::is_regular_file(recovered));

    std::ifstream input(blocked, std::ios::binary);
    ASSERT_TRUE(input);
    const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(contents, "keep");
}

TEST(AsyncLoggerLifetimeTest, DefaultBuildDoesNotSetBuilderOutput)
{
    int constructions = 0;
    int destructions = 0;
    std::shared_ptr<AsyncLogger> defaultLogger;
    {
        AsyncLoggerBuilder builder;
        defaultLogger = builder.Build();
        ASSERT_NE(defaultLogger, nullptr);

        // 默认输出只属于先前那次 Build，Builder 后续配置不会替换已建日志器的目标。
        builder.BuildLoggerFlush<TrackedFlush>(constructions, destructions, std::make_unique<int>(42));
        auto configuredLogger = builder.Build();
        ASSERT_NE(configuredLogger, nullptr);
        EXPECT_EQ(constructions, 1);
        EXPECT_NO_THROW(configuredLogger->stop());
    }
    EXPECT_EQ(destructions, 1);
    EXPECT_NO_THROW(defaultLogger->stop());
    EXPECT_NO_THROW(defaultLogger->stop());
}

TEST(AsyncLoggerLifetimeTest, LastOwnerAutomaticallyReclaimsWorkerAndOutput)
{
    int constructions = 0;
    int destructions = 0;
    std::shared_ptr<AsyncLogger> logger;
    {
        AsyncLoggerBuilder builder;
        builder.BuildLoggerFlush<TrackedFlush>(constructions, destructions, std::make_unique<int>(42));
        logger = builder.Build();
    }
    EXPECT_EQ(constructions, 1);
    EXPECT_EQ(destructions, 0);
    auto anotherOwner = logger;
    std::weak_ptr<AsyncLogger> observer = logger;
    logger.reset();
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(destructions, 0);

    // 无需显式 stop；弱引用过期且输出析构，验证不存在回调反向强持有日志器的循环。
    anotherOwner.reset();
    EXPECT_TRUE(observer.expired());
    EXPECT_EQ(destructions, 1);
}

TEST(AsyncLoggerLifetimeTest, ReusingBuilderCreatesSeparateLoggersSharingConfiguredOutput)
{
    int constructions = 0;
    int destructions = 0;
    std::shared_ptr<AsyncLogger> first;
    std::shared_ptr<AsyncLogger> second;
    const int workersBefore = Thread::NumCreated();
    {
        AsyncLoggerBuilder builder;
        builder.BuildLoggerFlush<TrackedFlush>(constructions, destructions, std::make_unique<int>(42));
        first = builder.Build();
        second = builder.Build();
        EXPECT_NE(first, second);
        EXPECT_EQ(Thread::NumCreated(), workersBefore + 2);
        EXPECT_EQ(constructions, 1);
    }
    EXPECT_NO_THROW(first->stop());
    EXPECT_NO_THROW(first->stop());
    first.reset();
    EXPECT_EQ(destructions, 0);
    EXPECT_NO_THROW(second->stop());
    second.reset();
    EXPECT_EQ(destructions, 1);
}
