#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "logflush.h"


using namespace dlog;


namespace
{

    class FileFlushTest : public ::testing::Test
    {
    protected:

        void SetUp() override
        {
            const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
            m_directory = std::filesystem::temp_directory_path() / ("dlog_fileflush_test_" + std::to_string(suffix));
            std::filesystem::create_directories(m_directory);
        }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove_all(m_directory, error);
            EXPECT_FALSE(error) << error.message();
        }

        static std::string readFile(const std::filesystem::path &path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("failed to read test file: " + path.string());
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        std::vector<std::filesystem::path> files() const
        {
            std::vector<std::filesystem::path> result;
            for (const auto &entry : std::filesystem::directory_iterator(m_directory)) result.push_back(entry.path());
            std::sort(result.begin(), result.end());
            return result;
        }

        std::filesystem::path m_directory;
    };

} // namespace


TEST(StdoutFlushTest, WritesCompleteBinaryDataAndRespectsExplicitLength)
{
    StdoutFlush output;
    LogFlush &target = output;
    std::string payload(5000, 'A');
    payload[2500] = '\0';
    const char tail[] = {'t', 'a', 'i', 'l', '!'};

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(target.writeAndFlush("prefix|", 7));
    EXPECT_NO_THROW(target.writeAndFlush(payload.data(), payload.size()));
    EXPECT_NO_THROW(target.writeAndFlush(tail, 4));
    const std::string actual = testing::internal::GetCapturedStdout();

    EXPECT_EQ(actual, "prefix|" + payload + "tail");
}

TEST(StdoutFlushTest, EmptyWritesProduceNoOutput)
{
    StdoutFlush output;

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(output.writeAndFlush(nullptr, 0));
    EXPECT_NO_THROW(output.writeAndFlush("ignored", 0));
    const std::string actual = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(actual.empty());
}

TEST(StdoutFlushTest, RejectsNullDataWithPositiveLength)
{
    StdoutFlush output;

    EXPECT_THROW(output.writeAndFlush(nullptr, 1), std::invalid_argument);
}

TEST(StdoutFlushTest, RejectsLengthThatCannotBeRepresentedByStreamsize)
{
    const size_t maxLength = static_cast<size_t>(std::numeric_limits<std::streamsize>::max());
    if (maxLength == std::numeric_limits<size_t>::max()) GTEST_SKIP();

    StdoutFlush output;
    const char data = 'x';
    EXPECT_THROW(output.writeAndFlush(&data, maxLength + 1), std::length_error);
}

TEST(StdoutFlushTest, ReportsFailedStreamState)
{
    StdoutFlush output;
    const auto oldState = std::cout.rdstate();
    std::cout.setstate(std::ios::badbit);

    EXPECT_THROW(output.writeAndFlush("x", 1), std::runtime_error);
    EXPECT_NO_THROW(output.writeAndFlush(nullptr, 0));

    std::cout.clear(oldState);
}

TEST(StdoutFlushTest, FlushesNonemptyWritesAndIgnoresEmptyWrites)
{
    class CountingBuffer : public std::stringbuf
    {
    public:

        int flushes = 0;
        int sync() override
        {
            ++flushes;
            return std::stringbuf::sync();
        }
    } buffer;

    StdoutFlush output;
    auto *oldBuffer = std::cout.rdbuf(&buffer);
    EXPECT_NO_THROW(output.writeAndFlush("data", 4));
    EXPECT_NO_THROW(output.writeAndFlush(nullptr, 0));
    EXPECT_NO_THROW(output.writeAndFlush("ignored", 0));
    std::cout.rdbuf(oldBuffer);

    EXPECT_EQ(buffer.str(), "data");
    EXPECT_EQ(buffer.flushes, 1);
}

TEST_F(FileFlushTest, NormalFileIsReadableAfterEachWriteAndPreservesBinaryData)
{
    const auto filename = m_directory / std::filesystem::u8path(u8"\u65e5\u5fd7 \U0001f680.log");
    std::string payload(5000, 'A');
    payload[2500] = '\0';
    std::unique_ptr<LogFlush> output = std::make_unique<FileFlush>(filename);
    const char tail[] = {'t', 'a', 'i', 'l', '!'};

    output->writeAndFlush(payload.data(), payload.size());
    EXPECT_EQ(readFile(filename), payload);
    output->writeAndFlush(tail, 4);
    EXPECT_EQ(readFile(filename), payload + "tail");
    ASSERT_EQ(files().size(), 1u);
    EXPECT_EQ(files().front(), filename);
}

TEST_F(FileFlushTest, ReopeningNormalFileAppendsExistingContent)
{
    const auto filename = m_directory / "app.log";
    {
        FileFlush output(filename);
        output.writeAndFlush("first|", 6);
    }
    FileFlush output(filename);
    output.writeAndFlush("second", 6);

    EXPECT_EQ(readFile(filename), "first|second");
}

TEST_F(FileFlushTest, RollingFileFlushesBeforeAndAfterRotation)
{
    const auto basePath = m_directory / std::filesystem::u8path(u8"\u8f6e\u8f6c \U0001f680");
    std::unique_ptr<LogFlush> output = std::make_unique<RollFileFlush>(basePath, 4);
    output->writeAndFlush("abcd", 4);
    ASSERT_EQ(files().size(), 1u);
    EXPECT_EQ(readFile(files().front()), "abcd");

    EXPECT_NO_THROW(output->writeAndFlush(nullptr, 0));
    EXPECT_NO_THROW(output->writeAndFlush("ignored", 0));
    EXPECT_THROW(output->writeAndFlush(nullptr, 1), std::invalid_argument);
    EXPECT_EQ(files().size(), 1u);

    const char tail[] = {'e', '\0', 'f', '!'};
    output->writeAndFlush(tail, 3);
    const auto paths = files();
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(readFile(paths[0]), "abcd");
    EXPECT_EQ(readFile(paths[1]), std::string(tail, 3));
}

TEST_F(FileFlushTest, EmptyAndInvalidWritesLeaveNormalFileUnchanged)
{
    const auto filename = m_directory / "app.log";
    FileFlush output(filename);

    EXPECT_NO_THROW(output.writeAndFlush(nullptr, 0));
    EXPECT_NO_THROW(output.writeAndFlush("ignored", 0));
    EXPECT_THROW(output.writeAndFlush(nullptr, 1), std::invalid_argument);
    EXPECT_TRUE(readFile(filename).empty());

    output.writeAndFlush("valid", 5);
    EXPECT_EQ(readFile(filename), "valid");
}

TEST_F(FileFlushTest, ReportsMissingParentDirectoryInBothModes)
{
    const auto path = m_directory / "missing" / "app";

    EXPECT_THROW({ FileFlush output(path); }, std::runtime_error);
    EXPECT_THROW({ RollFileFlush output(path, 4); }, std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(path.parent_path()));
}

TEST_F(FileFlushTest, ReopeningRollingFileKeepsOldContentsAndUsesNextIndex)
{
    const auto basePath = m_directory / "app";
    {
        RollFileFlush output(basePath, 4);
        output.writeAndFlush("first", 5);
    }
    RollFileFlush output(basePath, 4);
    output.writeAndFlush("next", 4);

    const auto paths = files();
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(readFile(paths[0]), "first");
    EXPECT_EQ(readFile(paths[1]), "next");
}

TEST_F(FileFlushTest, RejectsEmptyPathsInBothModes)
{
    const std::filesystem::path empty;
    EXPECT_THROW({ FileFlush output(empty); }, std::invalid_argument);
    EXPECT_THROW({ RollFileFlush output(empty, 4); }, std::invalid_argument);
    EXPECT_TRUE(files().empty());
}

TEST(LogFlushFactoryTest, CreatesStdoutFlushWithNoArguments)
{
    const auto output = LogFlushFactory::CreateLogFlush<StdoutFlush>();
    ASSERT_NE(std::dynamic_pointer_cast<StdoutFlush>(output), nullptr);

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(output->writeAndFlush("factory", 7));
    const auto actual = testing::internal::GetCapturedStdout();
    EXPECT_EQ(actual, "factory");
}

TEST_F(FileFlushTest, FactoryCreatesNormalFileWithNativePath)
{
    const auto filename = m_directory / std::filesystem::u8path(u8"\u5de5\u5382 \U0001f680.log");
    const auto output = LogFlushFactory::CreateLogFlush<FileFlush>(filename);
    ASSERT_NE(std::dynamic_pointer_cast<FileFlush>(output), nullptr);
    const char bytes[] = {'a', '\0', 'b'};

    output->writeAndFlush(bytes, sizeof(bytes));

    EXPECT_EQ(readFile(filename), std::string(bytes, sizeof(bytes)));
    ASSERT_EQ(files().size(), 1u);
    EXPECT_EQ(files().front(), filename);
}

TEST_F(FileFlushTest, FactoryCreatesRollingFileWithNativePathAndSize)
{
    const auto basePath = m_directory / std::filesystem::u8path(u8"\u5de5\u5382 \U0001f680");
    const auto output = LogFlushFactory::CreateLogFlush<RollFileFlush>(basePath, 4);
    ASSERT_NE(std::dynamic_pointer_cast<RollFileFlush>(output), nullptr);

    output->writeAndFlush("abcd", 4);
    output->writeAndFlush("e", 1);

    const auto paths = files();
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(readFile(paths[0]), "abcd");
    EXPECT_EQ(readFile(paths[1]), "e");
}

TEST_F(FileFlushTest, FactoryPropagatesConstructionErrors)
{
    const std::filesystem::path empty;
    EXPECT_THROW(LogFlushFactory::CreateLogFlush<FileFlush>(empty), std::invalid_argument);
    EXPECT_THROW(LogFlushFactory::CreateLogFlush<RollFileFlush>(empty, 4), std::invalid_argument);

    const auto missing = m_directory / "missing" / "app";
    EXPECT_THROW(LogFlushFactory::CreateLogFlush<FileFlush>(missing), std::runtime_error);
    EXPECT_THROW(LogFlushFactory::CreateLogFlush<RollFileFlush>(missing, 4), std::runtime_error);
    EXPECT_TRUE(files().empty());
}

TEST(LogFlushFactoryTest, ForwardsLvalueAndMoveOnlyArgumentsAndSharesLifetime)
{
    // 引用参数和不可复制参数分别验证左值保留、右值移动，避免退化为按值复制。
    class TrackedFlush : public LogFlush
    {
    public:

        TrackedFlush(int &destructions, std::unique_ptr<int> value) : destructions(destructions), value(std::move(value)) {}
        ~TrackedFlush() override { ++destructions; }
        void writeAndFlush(const char *, size_t) override {}

        int &destructions;
        std::unique_ptr<int> value;
    };

    int destructions = 0;
    auto value = std::make_unique<int>(42);
    const auto *original = value.get();
    auto output = LogFlushFactory::CreateLogFlush<TrackedFlush>(destructions, std::move(value));
    ASSERT_EQ(value, nullptr);
    const auto *derived = dynamic_cast<TrackedFlush *>(output.get());
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->value.get(), original);
    EXPECT_EQ(*derived->value, 42);

    auto shared = output;
    output.reset();
    EXPECT_EQ(destructions, 0);
    shared.reset();
    EXPECT_EQ(destructions, 1);
}
