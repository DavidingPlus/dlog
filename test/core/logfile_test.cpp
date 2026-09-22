#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "logfile.h"


namespace
{

    std::filesystem::path makeTempBasename()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() / ("dlog_logfile_test_" + std::to_string(suffix));
    }

    std::vector<std::filesystem::path> findLogFiles(const std::filesystem::path &basename)
    {
        std::vector<std::filesystem::path> files;
        const std::filesystem::path directory = basename.has_parent_path() ? basename.parent_path() : std::filesystem::path(".");
        const std::string prefix = basename.filename().string() + '.';

        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory))
        {
            const std::string filename = entry.path().filename().string();
            constexpr std::string_view suffix = ".log";
            if (filename.rfind(prefix, 0) == 0 && filename.size() >= suffix.size() &&
                filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                files.push_back(entry.path());
            }
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    std::string readFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }


    class LogFileTest : public ::testing::Test
    {

    protected:

        void SetUp() override { m_basename = makeTempBasename(); }

        void TearDown() override
        {
            for (const auto &file : findLogFiles(m_basename))
            {
                std::error_code error;
                std::filesystem::remove(file, error);
                EXPECT_FALSE(error) << error.message();
            }
        }

        std::filesystem::path m_basename;
    };

} // namespace


TEST_F(LogFileTest, CreatesDateAndIndexBasedFileName)
{
    {
        LogFile logFile(m_basename.string(), 1024, 60);
        logFile.append("first\n", 6);
        logFile.flush();
    }

    const auto files = findLogFiles(m_basename);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_NE(files.front().filename().string().find(".0.log"), std::string::npos);
    EXPECT_EQ(readFile(files.front()), "first\n");
}

TEST_F(LogFileTest, IncrementsIndexBeforeWritingWhenFileSizeWouldBeExceeded)
{
    {
        LogFile logFile(m_basename.string(), 1, 60);
        logFile.append("a", 1);
        logFile.append("b", 1); // 当前文件再写入 1 字节会超限，因此先切换到序号 1 的文件。
        logFile.append("c", 1);
        logFile.flush();
    }

    const auto files = findLogFiles(m_basename);
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(readFile(files[0]), "a");
    EXPECT_EQ(readFile(files[1]), "b");
    EXPECT_EQ(readFile(files[2]), "c");
    EXPECT_NE(files[0].filename().string().find(".0.log"), std::string::npos);
    EXPECT_NE(files[1].filename().string().find(".1.log"), std::string::npos);
    EXPECT_NE(files[2].filename().string().find(".2.log"), std::string::npos);
}

TEST_F(LogFileTest, UsesNextIndexAfterRestart)
{
    {
        LogFile logFile(m_basename.string(), 1024, 60);
        logFile.append("first run\n", 10);
        logFile.flush();
    }

    {
        LogFile logFile(m_basename.string(), 1024, 60);
        logFile.append("second run\n", 11);
        logFile.flush();
    }

    const auto files = findLogFiles(m_basename);
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(readFile(files[0]), "first run\n");
    EXPECT_EQ(readFile(files[1]), "second run\n");
    EXPECT_NE(files[0].filename().string().find(".0.log"), std::string::npos);
    EXPECT_NE(files[1].filename().string().find(".1.log"), std::string::npos);
}
