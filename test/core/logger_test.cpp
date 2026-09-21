#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>

#include "logger.h"


namespace
{

    std::string_view basenameOf(std::string_view path)
    {
        auto separator = path.find_last_of("/\\");
        return std::string_view::npos == separator ? path : path.substr(separator + 1);
    }

} // namespace


TEST(FileNameViewTest, ExposesAStringView)
{
    FileNameView file("Logger.cc");

    static_assert(std::is_same_v<decltype(file.view()), const std::string_view &>);
    static_assert(noexcept(file.view()));

    EXPECT_EQ(file.view(), "Logger.cc");
    EXPECT_EQ(file.view().size(), 9u);
}

TEST(FileNameViewTest, KeepsAPlainFileNameUnchanged)
{
    FileNameView file("Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, ExtractsFileNameFromUnixPath)
{
    FileNameView file("/home/user/project/logger/Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, ExtractsFileNameFromWindowsPath)
{
    FileNameView file(R"(D:\Workspace\dlog\src\core\logger.h)");

    EXPECT_EQ(file.view(), "logger.h");
}

TEST(FileNameViewTest, SupportsMixedPathSeparators)
{
    FileNameView file(R"(project\src/core\Logger.cc)");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, UsesTheLastPathSeparator)
{
    FileNameView file("/home/user/project/logger/Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
    EXPECT_NE(file.view(), "logger/Logger.cc");
}

TEST(FileNameViewTest, ReturnsAnEmptyViewForAnEmptyPath)
{
    FileNameView file("");

    EXPECT_TRUE(file.view().empty());
    EXPECT_EQ(file.view().size(), 0u);
}

TEST(FileNameViewTest, ReturnsAnEmptyViewWhenPathEndsWithUnixSeparator)
{
    FileNameView file("/home/user/project/logger/");

    EXPECT_TRUE(file.view().empty());
}

TEST(FileNameViewTest, ReturnsAnEmptyViewWhenPathEndsWithWindowsSeparator)
{
    FileNameView file(R"(D:\Workspace\dlog\src\core\)");

    EXPECT_TRUE(file.view().empty());
}

TEST(FileNameViewTest, HandlesRepeatedPathSeparators)
{
    FileNameView file("/home//user///Logger.cc");

    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, HandlesRootOnlyPaths)
{
    FileNameView unixRoot("/");
    FileNameView windowsRoot(R"(\)");

    EXPECT_TRUE(unixRoot.view().empty());
    EXPECT_TRUE(windowsRoot.view().empty());
}

TEST(FileNameViewTest, PreservesTheOriginalString)
{
    std::string path = "/home/user/project/Logger.cc";
    const std::string original = path;

    FileNameView file(path.c_str());

    EXPECT_EQ(path, original);
    EXPECT_EQ(file.view(), "Logger.cc");
}

TEST(FileNameViewTest, DoesNotCopyTheFileNameData)
{
    std::string path = "/home/user/project/Logger.cc";
    FileNameView file(path.c_str());

    const auto separator = path.find_last_of("/\\");
    ASSERT_NE(separator, std::string::npos);

    EXPECT_EQ(file.view().data(), path.data() + separator + 1);
}

TEST(FileNameViewTest, WorksWithTheFileMacro)
{
    const std::string_view sourceFile = __FILE__;
    const std::string_view expected = basenameOf(sourceFile);

    FileNameView file(__FILE__);

    EXPECT_EQ(file.view(), expected);
}

TEST(FileNameViewTest, ViewCanOutliveATemporaryFileNameView)
{
    const std::string_view view = FileNameView("temporary/Logger.cc").view();

    // FileNameView 临时对象已经销毁，但字符串字面量具有静态存储期，仍然有效。
    EXPECT_EQ(view, "Logger.cc");
}
