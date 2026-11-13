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
    StdoutFlush logFlush;
    LogFlush &target = logFlush;
    std::string payload(5000, 'A');
    payload[2500] = '\0';
    const char tail[] = {'t', 'a', 'i', 'l', '!'};

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(target.write("prefix|", 7));
    EXPECT_NO_THROW(target.write(payload.data(), payload.size()));
    EXPECT_NO_THROW(target.write(tail, 4));
    const std::string actual = testing::internal::GetCapturedStdout();

    EXPECT_EQ(actual, "prefix|" + payload + "tail");
}

TEST(StdoutFlushTest, EmptyWritesProduceNoOutput)
{
    StdoutFlush logFlush;

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(logFlush.write(nullptr, 0));
    EXPECT_NO_THROW(logFlush.write("ignored", 0));
    const std::string actual = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(actual.empty());
}

TEST(StdoutFlushTest, RejectsNullDataWithPositiveLength)
{
    StdoutFlush logFlush;

    EXPECT_THROW(logFlush.write(nullptr, 1), std::invalid_argument);
}

TEST(StdoutFlushTest, RejectsLengthThatCannotBeRepresentedByStreamsize)
{
    const size_t maxLength = static_cast<size_t>(std::numeric_limits<std::streamsize>::max());
    if (maxLength == std::numeric_limits<size_t>::max()) GTEST_SKIP();

    StdoutFlush logFlush;
    const char data = 'x';
    EXPECT_THROW(logFlush.write(&data, maxLength + 1), std::length_error);
}

TEST(StdoutFlushTest, ReportsFailedStreamState)
{
    StdoutFlush logFlush;
    const auto oldState = std::cout.rdstate();
    std::cout.setstate(std::ios::badbit);

    EXPECT_THROW(logFlush.write("x", 1), std::runtime_error);
    EXPECT_THROW(logFlush.flush(), std::runtime_error);
    EXPECT_NO_THROW(logFlush.write(nullptr, 0));

    std::cout.clear(oldState);
}

TEST(StdoutFlushTest, WritesDoNotFlushUntilExplicitlyRequested)
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

    StdoutFlush logFlush;
    auto *oldBuffer = std::cout.rdbuf(&buffer);
    EXPECT_NO_THROW(logFlush.write("data", 4));
    EXPECT_NO_THROW(logFlush.write(nullptr, 0));
    EXPECT_NO_THROW(logFlush.write("ignored", 0));
    EXPECT_EQ(buffer.flushes, 0);
    EXPECT_NO_THROW(logFlush.flush());
    std::cout.rdbuf(oldBuffer);

    EXPECT_EQ(buffer.str(), "data");
    EXPECT_EQ(buffer.flushes, 1);
}

TEST_F(FileFlushTest, NormalFileIsReadableAfterEachWriteAndPreservesBinaryData)
{
    const auto filename = m_directory / std::filesystem::u8path(u8"\u65e5\u5fd7 \U0001f680.log");
    std::string payload(5000, 'A');
    payload[2500] = '\0';
    std::unique_ptr<LogFlush> logFlush = std::make_unique<FileFlush>(filename);
    const char tail[] = {'t', 'a', 'i', 'l', '!'};

    logFlush->write(payload.data(), payload.size());
    logFlush->flush();
    EXPECT_EQ(readFile(filename), payload);
    logFlush->write(tail, 4);
    logFlush->flush();
    EXPECT_EQ(readFile(filename), payload + "tail");
    ASSERT_EQ(files().size(), 1u);
    EXPECT_EQ(files().front(), filename);
}

TEST_F(FileFlushTest, MultipleWritesCanShareOneFlush)
{
    const auto filename = m_directory / "batch.log";
    FileFlush logFlush(filename);
    logFlush.write("first", 5);
    logFlush.write("\0tail", 5);
    logFlush.flush();
    EXPECT_EQ(readFile(filename), std::string("first\0tail", 10));
}

TEST_F(FileFlushTest, ReopeningNormalFileAppendsExistingContent)
{
    const auto filename = m_directory / "app.log";
    {
        FileFlush logFlush(filename);
        logFlush.write("first|", 6);
    }
    FileFlush logFlush(filename);
    logFlush.write("second", 6);
    logFlush.flush();

    EXPECT_EQ(readFile(filename), "first|second");
}

TEST_F(FileFlushTest, RollingFileFlushesBeforeAndAfterRotation)
{
    const auto basePath = m_directory / std::filesystem::u8path(u8"\u8f6e\u8f6c \U0001f680");
    std::unique_ptr<LogFlush> logFlush = std::make_unique<RollFileFlush>(basePath, 4);
    logFlush->write("abcd", 4);
    logFlush->flush();
    ASSERT_EQ(files().size(), 1u);
    EXPECT_EQ(readFile(files().front()), "abcd");

    EXPECT_NO_THROW(logFlush->write(nullptr, 0));
    EXPECT_NO_THROW(logFlush->write("ignored", 0));
    EXPECT_THROW(logFlush->write(nullptr, 1), std::invalid_argument);
    EXPECT_EQ(files().size(), 1u);

    const char tail[] = {'e', '\0', 'f', '!'};
    logFlush->write(tail, 3);
    logFlush->flush();
    const auto paths = files();
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(readFile(paths[0]), "abcd");
    EXPECT_EQ(readFile(paths[1]), std::string(tail, 3));
}

TEST_F(FileFlushTest, EmptyAndInvalidWritesLeaveNormalFileUnchanged)
{
    const auto filename = m_directory / "app.log";
    FileFlush logFlush(filename);

    EXPECT_NO_THROW(logFlush.write(nullptr, 0));
    EXPECT_NO_THROW(logFlush.write("ignored", 0));
    EXPECT_THROW(logFlush.write(nullptr, 1), std::invalid_argument);
    EXPECT_TRUE(readFile(filename).empty());

    logFlush.write("valid", 5);
    logFlush.flush();
    EXPECT_EQ(readFile(filename), "valid");
}

TEST_F(FileFlushTest, CreatesMissingParentDirectoriesInBothModes)
{
    const auto normalPath = m_directory / "normal" / "nested" / "app.log";
    const auto rollingPath = m_directory / "rolling" / "nested" / "app";
    FileFlush normal(normalPath);
    RollFileFlush rolling(rollingPath, 4);
    normal.write("plain", 5);
    normal.flush();
    rolling.write("roll", 4);
    rolling.flush();

    EXPECT_EQ(readFile(normalPath), "plain");
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(rollingPath.parent_path())) paths.push_back(entry.path());
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(readFile(paths.front()), "roll");
}

TEST_F(FileFlushTest, ReopeningRollingFileKeepsOldContentsAndUsesNextIndex)
{
    const auto basePath = m_directory / "app";
    {
        RollFileFlush logFlush(basePath, 4);
        logFlush.write("first", 5);
    }
    RollFileFlush logFlush(basePath, 4);
    logFlush.write("next", 4);
    logFlush.flush();

    const auto paths = files();
    ASSERT_EQ(paths.size(), 2u);
    EXPECT_EQ(readFile(paths[0]), "first");
    EXPECT_EQ(readFile(paths[1]), "next");
}

TEST_F(FileFlushTest, RejectsEmptyPathsInBothModes)
{
    const std::filesystem::path empty;
    EXPECT_THROW({ FileFlush logFlush(empty); }, std::invalid_argument);
    EXPECT_THROW({ RollFileFlush logFlush(empty, 4); }, std::invalid_argument);
    EXPECT_TRUE(files().empty());
}

TEST(LogFlushFactoryTest, CreatesStdoutFlushWithNoArguments)
{
    const auto logFlush = LogFlushFactory::CreateLogFlush<StdoutFlush>();
    ASSERT_NE(std::dynamic_pointer_cast<StdoutFlush>(logFlush), nullptr);

    testing::internal::CaptureStdout();
    EXPECT_NO_THROW(logFlush->write("factory", 7));
    const auto actual = testing::internal::GetCapturedStdout();
    EXPECT_EQ(actual, "factory");
}

TEST_F(FileFlushTest, FactoryCreatesNormalFileWithNativePath)
{
    const auto filename = m_directory / std::filesystem::u8path(u8"\u5de5\u5382 \U0001f680.log");
    const auto logFlush = LogFlushFactory::CreateLogFlush<FileFlush>(filename);
    ASSERT_NE(std::dynamic_pointer_cast<FileFlush>(logFlush), nullptr);
    const char bytes[] = {'a', '\0', 'b'};

    logFlush->write(bytes, sizeof(bytes));
    logFlush->flush();

    EXPECT_EQ(readFile(filename), std::string(bytes, sizeof(bytes)));
    ASSERT_EQ(files().size(), 1u);
    EXPECT_EQ(files().front(), filename);
}

TEST_F(FileFlushTest, FactoryCreatesRollingFileWithNativePathAndSize)
{
    const auto basePath = m_directory / std::filesystem::u8path(u8"\u5de5\u5382 \U0001f680");
    const auto logFlush = LogFlushFactory::CreateLogFlush<RollFileFlush>(basePath, 4);
    ASSERT_NE(std::dynamic_pointer_cast<RollFileFlush>(logFlush), nullptr);

    logFlush->write("abcd", 4);
    logFlush->flush();
    logFlush->write("e", 1);
    logFlush->flush();

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

    const auto blocked = m_directory / "blocked";
    {
        std::ofstream(blocked, std::ios::binary) << "keep";
    }
    const auto path = blocked / "nested" / "app";
    EXPECT_THROW(LogFlushFactory::CreateLogFlush<FileFlush>(path), std::filesystem::filesystem_error);
    EXPECT_THROW(LogFlushFactory::CreateLogFlush<RollFileFlush>(path, 4), std::filesystem::filesystem_error);
    EXPECT_EQ(readFile(blocked), "keep");
    EXPECT_EQ(files().size(), 1u);
}

TEST(LogFlushFactoryTest, ForwardsLvalueAndMoveOnlyArgumentsAndSharesLifetime)
{
    // 引用参数和不可复制参数分别验证左值保留、右值移动，避免退化为按值复制。
    class TrackedFlush : public LogFlush
    {
    public:
        void flush() override {}

        TrackedFlush(int &destructions, std::unique_ptr<int> value) : destructions(destructions), value(std::move(value)) {}
        ~TrackedFlush() override { ++destructions; }
        void write(const char *, size_t) override {}

        int &destructions;
        std::unique_ptr<int> value;
    };

    int destructions = 0;
    auto value = std::make_unique<int>(42);
    const auto *original = value.get();
    auto logFlush = LogFlushFactory::CreateLogFlush<TrackedFlush>(destructions, std::move(value));
    ASSERT_EQ(value, nullptr);
    const auto *derived = dynamic_cast<TrackedFlush *>(logFlush.get());
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->value.get(), original);
    EXPECT_EQ(*derived->value, 42);

    auto shared = logFlush;
    logFlush.reset();
    EXPECT_EQ(destructions, 0);
    shared.reset();
    EXPECT_EQ(destructions, 1);
}
