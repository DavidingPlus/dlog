#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "fileutil.h"


using namespace dlog;


namespace
{

    class CurrentPathGuard
    {
    public:
        explicit CurrentPathGuard(const std::filesystem::path &path) : m_previous(std::filesystem::current_path()) { std::filesystem::current_path(path); }
        ~CurrentPathGuard()
        {
            std::error_code error;
            std::filesystem::current_path(m_previous, error);
        }
        CurrentPathGuard(const CurrentPathGuard &) = delete;
        CurrentPathGuard &operator=(const CurrentPathGuard &) = delete;

    private:
        std::filesystem::path m_previous;
    };

    std::filesystem::path makeTempFilePath()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() /
               ("dlog_fileutil_test_" + std::to_string(suffix) + ".log");
    }

    std::string readBinaryFile(const std::filesystem::path &filePath)
    {
        std::ifstream input(filePath, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }


    class FileUtilTest : public ::testing::Test
    {

    protected:

        void SetUp() override { m_filePath = makeTempFilePath(); }

        void TearDown() override
        {
            std::error_code error;
            std::filesystem::remove(m_filePath, error);
            EXPECT_FALSE(error) << error.message();
        }

        std::filesystem::path m_filePath;
    };

} // namespace


TEST(FileUtilTypeTest, IsNonCopyableAndNonMovable)
{
    EXPECT_FALSE(std::is_copy_constructible_v<FileUtil>);
    EXPECT_FALSE(std::is_copy_assignable_v<FileUtil>);
    EXPECT_FALSE(std::is_move_constructible_v<FileUtil>);
    EXPECT_FALSE(std::is_move_assignable_v<FileUtil>);
}

TEST(FileUtilTypeTest, ExecutablePathNamesCurrentProcessAndIgnoresWorkingDirectory)
{
    const auto executable = FileUtil::executableFullPath();
    const auto args = testing::internal::GetArgvs();
    ASSERT_FALSE(args.empty());
    EXPECT_TRUE(executable.is_absolute());
    EXPECT_TRUE(std::filesystem::equivalent(executable, std::filesystem::path(args.front())));
    EXPECT_EQ(FileUtil::executableDirectory(), executable.parent_path());

    CurrentPathGuard currentPath(std::filesystem::temp_directory_path());
    EXPECT_EQ(FileUtil::executableFullPath(), executable);
    EXPECT_EQ(FileUtil::executableDirectory(), executable.parent_path());
}

TEST_F(FileUtilTest, ConstructorCreatesAnEmptyFile)
{
    {
        FileUtil file(m_filePath.string());

        EXPECT_EQ(file.writtenBytes(), 0);
        EXPECT_TRUE(std::filesystem::exists(m_filePath));
    }

    ASSERT_TRUE(std::filesystem::exists(m_filePath));
    EXPECT_EQ(std::filesystem::file_size(m_filePath), 0u);
}

TEST_F(FileUtilTest, AppendsDataAndReportsWrittenBytes)
{
    const std::string first = "first line\n";
    const std::string second = "second line\n";

    {
        FileUtil file(m_filePath.string());

        file.append(first.data(), first.size());
        EXPECT_EQ(file.writtenBytes(), static_cast<int64_t>(first.size()));

        file.append(second.data(), second.size());
        EXPECT_EQ(file.writtenBytes(), static_cast<int64_t>(first.size() + second.size()));

        file.flush();
    }

    EXPECT_EQ(readBinaryFile(m_filePath), first + second);
}

TEST_F(FileUtilTest, FlushMakesBufferedDataVisible)
{
    const std::string data = "data waiting in the stdio buffer";

    FileUtil file(m_filePath.string());
    file.append(data.data(), data.size());

    ASSERT_NO_THROW(file.flush());
    ASSERT_TRUE(std::filesystem::exists(m_filePath));
    EXPECT_EQ(std::filesystem::file_size(m_filePath), data.size());
}

TEST_F(FileUtilTest, DestructorFlushesPendingData)
{
    const std::string data = "flushed by destructor";

    {
        FileUtil file(m_filePath.string());
        file.append(data.data(), data.size());
    }

    EXPECT_EQ(readBinaryFile(m_filePath), data);
}

TEST_F(FileUtilTest, AppendsAfterExistingFileContent)
{
    const std::string existing = "existing content\n";
    const std::string appended = "new content\n";

    {
        std::ofstream output(m_filePath, std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output.write(existing.data(), static_cast<std::streamsize>(existing.size()));
    }

    {
        FileUtil file(m_filePath.string());

        // writtenBytes() 只统计当前 FileUtil 实例追加的数据，不包含文件原有内容。
        EXPECT_EQ(file.writtenBytes(), 0);
        file.append(appended.data(), appended.size());
        EXPECT_EQ(file.writtenBytes(), static_cast<int64_t>(appended.size()));
    }

    EXPECT_EQ(readBinaryFile(m_filePath), existing + appended);
}

TEST_F(FileUtilTest, PreservesBinaryData)
{
    std::string data;
    data.push_back('\0');
    data.push_back('\x01');
    data.push_back('\n');
    data.push_back('\r');
    data.push_back(static_cast<char>(0xff));

    {
        FileUtil file(m_filePath.string());
        file.append(data.data(), data.size());
        file.flush();
    }

    EXPECT_EQ(readBinaryFile(m_filePath), data);
}

TEST_F(FileUtilTest, HandlesDataLargerThanInternalBuffer)
{
    constexpr size_t bufferSize = 64 * 1024;
    const size_t dataSize = bufferSize * 3 + 123;

    std::string data(dataSize, '\0');
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>(i % 251);

    {
        FileUtil file(m_filePath.string());
        file.append(data.data(), data.size());

        EXPECT_EQ(file.writtenBytes(), static_cast<int64_t>(data.size()));
        file.flush();
    }

    ASSERT_EQ(std::filesystem::file_size(m_filePath), data.size());
    EXPECT_EQ(readBinaryFile(m_filePath), data);
}

TEST_F(FileUtilTest, EmptyAppendDoesNotChangeFile)
{
    FileUtil file(m_filePath.string());
    file.append("ignored", 0);

    EXPECT_EQ(file.writtenBytes(), 0);
    file.flush();

    EXPECT_EQ(std::filesystem::file_size(m_filePath), 0u);
}

TEST_F(FileUtilTest, NullDataWithNonzeroLengthThrows)
{
    FileUtil file(m_filePath.string());

    EXPECT_THROW(file.append(nullptr, 1), std::invalid_argument);
    EXPECT_EQ(file.writtenBytes(), 0);
}

TEST_F(FileUtilTest, ConstructorThrowsWhenFileCannotBeOpened)
{
    const auto invalidPath = m_filePath.parent_path() /
                             ("dlog_fileutil_missing_parent_" + m_filePath.stem().string()) /
                             m_filePath.filename();

    EXPECT_THROW(
        {
            FileUtil file(invalidPath.string());
        },
        std::runtime_error);
}

TEST_F(FileUtilTest, SupportsNativeUnicodePathWithSpaces)
{
    auto filename = std::filesystem::u8path(u8"日志 🦄 ");
    filename += m_filePath.filename();
    m_filePath.replace_filename(filename);
    const std::string data("path\0data\n", 10);

    {
        FileUtil file(m_filePath);
        file.append(data.data(), data.size());
        file.flush();
    }

    EXPECT_TRUE(std::filesystem::exists(m_filePath));
    EXPECT_EQ(readBinaryFile(m_filePath), data);
}

TEST_F(FileUtilTest, RejectsEmptyPath)
{
    EXPECT_THROW({ FileUtil file(std::filesystem::path{}); }, std::invalid_argument);
}

TEST_F(FileUtilTest, RejectsEmbeddedNullBeforeOpeningPathPrefix)
{
    auto nativePath = m_filePath.native();
    nativePath.push_back(std::filesystem::path::value_type{});
    nativePath += std::filesystem::path("suffix").native();

    EXPECT_THROW({ FileUtil file{std::filesystem::path(nativePath)}; }, std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(m_filePath));
}

TEST_F(FileUtilTest, RelativePathUsesExecutableDirectoryAndNormalizesDotSegments)
{
    const auto workingDirectoryPath = m_filePath;
    m_filePath = FileUtil::executableDirectory() / m_filePath.filename();
    {
        CurrentPathGuard currentPath(workingDirectoryPath.parent_path());
        FileUtil file(std::filesystem::path("unused") / ".." / m_filePath.filename());
        file.append("relative", 8);
        file.flush();
    }

    EXPECT_EQ(readBinaryFile(m_filePath), "relative");
    EXPECT_FALSE(std::filesystem::exists(workingDirectoryPath));
    std::error_code error;
    std::filesystem::remove(workingDirectoryPath, error);
    EXPECT_FALSE(error) << error.message();
}

TEST_F(FileUtilTest, AbsolutePathIsUsedAfterWorkingDirectoryChanges)
{
    CurrentPathGuard currentPath(FileUtil::executableDirectory());
    FileUtil file(m_filePath);
    file.append("absolute", 8);
    file.flush();

    EXPECT_EQ(readBinaryFile(m_filePath), "absolute");
}
