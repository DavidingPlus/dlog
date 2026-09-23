#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "logfile.h"


namespace
{

    std::filesystem::path makeTempDirectory()
    {
        auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("dlog_logfile_test_" + std::to_string(suffix));
    }

    std::string currentDateString()
    {
        std::time_t now = std::time(nullptr);
        std::tm localTime{};

#if defined(_WIN32)
        if (0 != ::localtime_s(&localTime, &now)) throw std::runtime_error("localtime_s failed");
#else
        if (nullptr == ::localtime_r(&now, &localTime)) throw std::runtime_error("localtime_r failed");
#endif

        std::ostringstream date;
        date << std::put_time(&localTime, "%Y%m%d");
        return date.str();
    }

    std::filesystem::path makeLogPath(const std::filesystem::path &basePath, const std::string &date, std::string_view index) { return std::filesystem::path(basePath.string() + "." + date + "." + std::string(index) + ".log"); }

    std::vector<std::filesystem::path> findLogFiles(const std::filesystem::path &basePath)
    {
        std::vector<std::filesystem::path> files;
        std::filesystem::path directory = basePath.has_parent_path() ? basePath.parent_path() : std::filesystem::path(".");
        std::string prefix = basePath.filename().string() + '.';
        std::string_view suffix = ".log";

        for (auto &entry : std::filesystem::directory_iterator(directory))
        {
            std::string filename = entry.path().filename().string();
            if (0 == filename.rfind(prefix, 0) &&
                filename.size() >= suffix.size() &&
                filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) files.push_back(entry.path());
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    void writeFile(const std::filesystem::path &path, const std::string &data)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("failed to open test file for writing: " + path.string());

        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!output) throw std::runtime_error("failed to write test file: " + path.string());
    }

    std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("failed to open test file for reading: " + path.string());


        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    std::size_t countOccurrences(const std::string &text, std::string_view target)
    {
        std::size_t count = 0;
        std::size_t position = 0;
        while (std::string::npos != (position = text.find(target, position)))
        {
            ++count;
            position += target.size();
        }


        return count;
    }

    class CurrentPathGuard
    {

    public:

        explicit CurrentPathGuard(const std::filesystem::path &path) : m_previousPath(std::filesystem::current_path()) { std::filesystem::current_path(path); }

        ~CurrentPathGuard()
        {
            std::error_code error;
            std::filesystem::current_path(m_previousPath, error);
        }

        CurrentPathGuard(const CurrentPathGuard &) = delete;

        CurrentPathGuard &operator=(const CurrentPathGuard &) = delete;


    private:

        std::filesystem::path m_previousPath;
    };


    class LogFileTest : public ::testing::Test
    {

    protected:

        void SetUp() override
        {
            m_directory = makeTempDirectory();

            std::error_code error;
            std::filesystem::create_directories(m_directory, error);
            ASSERT_FALSE(error) << error.message();

            m_basePath = m_directory / "app";
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


TEST_F(LogFileTest, CreatesDateAndIndexBasedFileName)
{
    const std::string date = currentDateString();

    {
        LogFile logFile(m_basePath.string(), 1024, 60);
        logFile.append("first\n", 6);
        logFile.flush();
    }

    const auto files = findLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files.front(), makeLogPath(m_basePath, date, "0"));
    EXPECT_EQ(readFile(files.front()), "first\n");
}

TEST_F(LogFileTest, AppendsMultipleMessagesToSameFileUntilLimit)
{
    {
        LogFile logFile(m_basePath.string(), 4, 60);
        logFile.append("ab", 2);
        logFile.append("cd", 2);
        logFile.flush();
    }

    const auto files = findLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(readFile(files.front()), "abcd");
}

TEST_F(LogFileTest, RotatesOnlyWhenNextAppendWouldExceedLimit)
{
    const std::string date = currentDateString();

    {
        LogFile logFile(m_basePath.string(), 4, 60);
        logFile.append("abcd", 4); // 写入后刚好达到限制，不应轮转。
        logFile.append("e", 1);    // 下一条日志会超限，因此先轮转。
        logFile.flush();
    }

    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "0")), "abcd");
    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "1")), "e");
}

TEST_F(LogFileTest, AllowsOversizedFirstAppendAndRotatesBeforeNextAppend)
{
    const std::string date = currentDateString();

    {
        LogFile logFile(m_basePath.string(), 2, 60);
        logFile.append("long", 4); // 空文件允许完整写入一条超过限制的日志。
        logFile.append("next", 4);
        logFile.flush();
    }

    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "0")), "long");
    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "1")), "next");
}

TEST_F(LogFileTest, ZeroLengthAppendIsNoOp)
{
    const std::string date = currentDateString();

    {
        LogFile logFile(m_basePath.string(), 1, 60);
        logFile.append("oversized", 9);
        logFile.append(nullptr, 0);
        logFile.flush();
    }

    const auto files = findLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files.front(), makeLogPath(m_basePath, date, "0"));
    EXPECT_EQ(readFile(files.front()), "oversized");
}

TEST_F(LogFileTest, PreservesExplicitLengthAndEmbeddedNullCharacters)
{
    const std::string payload("abc\0def", 7);

    {
        LogFile logFile(m_basePath.string(), 1024, 60);
        logFile.append(payload.data(), payload.size());
        logFile.flush();
    }

    const auto files = findLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(readFile(files.front()), payload);
}

TEST_F(LogFileTest, DestructorFlushesCurrentFile)
{
    {
        LogFile logFile(m_basePath.string(), 1024, 60);
        logFile.append("written by destructor", 21);
    }

    const auto files = findLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(readFile(files.front()), "written by destructor");
}

TEST_F(LogFileTest, ManualRollCreatesNextFileWithoutDataLoss)
{
    const std::string date = currentDateString();

    {
        LogFile logFile(m_basePath.string(), 1024, 60);
        logFile.append("before roll", 11);
        logFile.rollFile();
        logFile.append("after roll", 10);
        logFile.flush();
    }

    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "0")), "before roll");
    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "1")), "after roll");
}

TEST_F(LogFileTest, UsesNextIndexAfterRestart)
{
    const std::string date = currentDateString();

    {
        LogFile logFile(m_basePath.string(), 1024, 60);
        logFile.append("first run\n", 10);
        logFile.flush();
    }

    {
        LogFile logFile(m_basePath.string(), 1024, 60);
        logFile.append("second run\n", 11);
        logFile.flush();
    }

    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "0")), "first run\n");
    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "1")), "second run\n");
}

TEST_F(LogFileTest, StartsAfterHighestExistingIndexAndIgnoresMalformedNames)
{
    const std::string date = currentDateString();
    const std::string otherDate = "19000101";

    writeFile(makeLogPath(m_basePath, date, "0"), "old zero");
    writeFile(makeLogPath(m_basePath, date, "2"), "old two");
    writeFile(makeLogPath(m_basePath, date, "bad"), "malformed");
    writeFile(makeLogPath(m_basePath, date, "-1"), "negative index");
    writeFile(makeLogPath(m_basePath, date, "999999999999999999999999"), "overflow index");
    writeFile(std::filesystem::path(m_basePath.string() + "." + date + "..log"), "empty index");
    writeFile(std::filesystem::path(m_basePath.string() + "." + date + ".3.txt"), "wrong suffix");
    writeFile(makeLogPath(m_basePath, otherDate, "99"), "wrong date");
    writeFile(std::filesystem::path(m_directory / ("other." + date + ".100.log")), "other base");

    {
        LogFile logFile(m_basePath.string(), 1024, 60);
        logFile.append("new", 3);
        logFile.flush();
    }

    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "0")), "old zero");
    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "2")), "old two");
    EXPECT_EQ(readFile(makeLogPath(m_basePath, date, "3")), "new");
    EXPECT_FALSE(std::filesystem::exists(makeLogPath(m_basePath, date, "1")));
}

TEST_F(LogFileTest, ThrowsWhenExistingIndexIsIntMax)
{
    const std::string date = currentDateString();
    const std::string maxIndex = std::to_string(std::numeric_limits<int>::max());
    writeFile(makeLogPath(m_basePath, date, maxIndex), "last valid index");

    EXPECT_THROW(
        {
            LogFile logFile(m_basePath.string(), 1024, 60);
        },
        std::overflow_error);
}

TEST_F(LogFileTest, ThrowsWhenBaseDirectoryDoesNotExist)
{
    const std::filesystem::path missingBasePath = m_directory / "missing" / "app";

    EXPECT_THROW(
        {
            LogFile logFile(missingBasePath.string(), 1024, 60);
        },
        std::filesystem::filesystem_error);
}

TEST_F(LogFileTest, SupportsRelativeBasePath)
{
    const std::filesystem::path relativeDirectory = m_directory / "relative";
    std::filesystem::create_directories(relativeDirectory);

    {
        CurrentPathGuard currentPath(m_directory);
        LogFile logFile("relative/app", 1024, 60);
        logFile.append("relative path", 13);
        logFile.flush();
    }

    const auto files = findLogFiles(relativeDirectory / "app");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(readFile(files.front()), "relative path");
}

TEST_F(LogFileTest, RejectsNullDataWhenLengthIsPositive)
{
    LogFile logFile(m_basePath.string(), 1024, 60);

    EXPECT_THROW(logFile.append(nullptr, 1), std::invalid_argument);

    logFile.flush();
    const auto files = findLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_TRUE(readFile(files.front()).empty());
}

TEST_F(LogFileTest, SerializesConcurrentAppends)
{
    constexpr int threadCount = 4;
    constexpr int messagesPerThread = 50;
    const std::string payload = "thread-message\n";

    {
        LogFile logFile(m_basePath.string(), 1024 * 1024, 60);
        std::vector<std::thread> threads;
        threads.reserve(threadCount);

        for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
        {
            threads.emplace_back([&logFile, &payload, messagesPerThread]()
                                 {
                for (int messageIndex = 0; messageIndex < messagesPerThread; ++messageIndex)
                {
                    logFile.append(payload.data(), payload.size());
                } });
        }

        for (std::thread &thread : threads)
        {
            thread.join();
        }

        logFile.flush();
    }

    const auto files = findLogFiles(m_basePath);
    ASSERT_EQ(files.size(), 1u);
    const std::string content = readFile(files.front());
    EXPECT_EQ(countOccurrences(content, payload), static_cast<std::size_t>(threadCount * messagesPerThread));
    EXPECT_EQ(content.size(), payload.size() * threadCount * messagesPerThread);
}
