#include "skiplist.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <streambuf>
#include <thread>
#include <utility>
#include <vector>


namespace
{

    // 用于单线程测试：捕获 SkipList 打印的日志，便于检查查询输出和层级结构。
    class CoutCapture
    {
    public:

        CoutCapture() : m_previousBuffer(std::cout.rdbuf(m_buffer.rdbuf())) {}

        ~CoutCapture()
        {
            std::cout.rdbuf(m_previousBuffer);
        }

        std::string str() const { return m_buffer.str(); }

    private:

        std::ostringstream m_buffer;
        std::streambuf *m_previousBuffer;
    };

    // 并发测试只丢弃日志，不把多个线程的输出写入同一个 ostringstream。
    // 这个缓冲区不保存任何可变状态，输出操作始终报告成功。
    class CoutDiscard
    {
    private:

        class Buffer : public std::streambuf
        {
        protected:

            int_type overflow(int_type character) override { return traits_type::not_eof(character); }
            std::streamsize xsputn(const char *, std::streamsize count) override { return count; }
            int sync() override { return 0; }
        };

    public:

        CoutDiscard() : m_previousBuffer(std::cout.rdbuf(&m_buffer)) {}
        ~CoutDiscard() { std::cout.rdbuf(m_previousBuffer); }

        CoutDiscard(const CoutDiscard &) = delete;
        CoutDiscard &operator=(const CoutDiscard &) = delete;

    private:

        Buffer m_buffer;
        std::streambuf *m_previousBuffer;
    };

    template <typename Function>
    std::string captureOutput(Function &&function)
    {
        CoutCapture capture;
        function();
        return capture.str();
    }

    template <typename Value>
    bool searchQuiet(SkipList<int, Value> &list, int key)
    {
        bool found = false;
        captureOutput([&]
                      { found = list.searchElement(key); });
        return found;
    }

    template <typename Value>
    void deleteQuiet(SkipList<int, Value> &list, int key)
    {
        captureOutput([&]
                      { list.deleteElement(key); });
    }

    template <typename Value>
    std::string displayQuiet(const SkipList<int, Value> &list)
    {
        return captureOutput([&]
                             { list.displayList(); });
    }

    std::vector<std::vector<int>> parseDisplayedLevels(const std::string &display)
    {
        std::vector<std::vector<int>> levels;
        std::istringstream input(display);
        std::string line;

        while (std::getline(input, line))
        {
            if (line.rfind("Level ", 0) != 0) continue;

            const size_t contentStart = line.find(": ");
            if (contentStart == std::string::npos) continue;

            std::vector<int> keys;
            size_t itemStart = contentStart + 2;
            while (itemStart < line.size())
            {
                const size_t itemEnd = line.find(';', itemStart);
                const size_t keyEnd = line.find(':', itemStart);
                if (keyEnd == std::string::npos || (itemEnd != std::string::npos && keyEnd > itemEnd)) break;

                keys.push_back(std::stoi(line.substr(itemStart, keyEnd - itemStart)));
                if (itemEnd == std::string::npos) break;
                itemStart = itemEnd + 1;
            }
            levels.push_back(std::move(keys));
        }

        return levels;
    }

    // 验证每层 key 严格递增，且高层中的每个节点也确实参加了它下面的每一层。
    void expectValidDisplayedStructure(const std::string &display,
                                       const std::vector<int> &expectedBottomKeys)
    {
        const std::vector<std::vector<int>> levels = parseDisplayedLevels(display);
        ASSERT_FALSE(levels.empty()) << "displayList() should always print level 0";
        EXPECT_EQ(levels.front(), expectedBottomKeys);
        if (levels.size() > 1) EXPECT_FALSE(levels.back().empty()) << "highest active level should not be empty";

        for (size_t level = 0; level < levels.size(); ++level)
        {
            EXPECT_TRUE(std::is_sorted(levels[level].begin(), levels[level].end()))
                << "keys are not sorted at level " << level;
            EXPECT_TRUE(std::adjacent_find(levels[level].begin(), levels[level].end()) == levels[level].end())
                << "duplicate key found at level " << level;

            for (int key : levels[level])
            {
                for (size_t lowerLevel = 0; lowerLevel < level; ++lowerLevel)
                {
                    EXPECT_NE(std::find(levels[lowerLevel].begin(), levels[lowerLevel].end(), key),
                              levels[lowerLevel].end())
                        << "key " << key << " is missing from lower level " << lowerLevel;
                }
            }
        }
    }

    std::string structureForSeed(unsigned int seed)
    {
        std::srand(seed);
        SkipList<int, int> list(6);

        captureOutput([&]
                      {
        for (int key = 0; key < 40; ++key) list.insertElement(key, key * 10); });

        return displayQuiet(list);
    }

    class SkipListTest : public ::testing::Test
    {
    protected:

        void SetUp() override
        {
            // rand/srand 状态由运行库维护；每个测试从固定 seed 开始，便于复现层级结构。
            std::srand(12345);
        }
    };

    // 文件保存和加载使用相对路径；每个测试在独立临时目录中运行，避免覆盖用户的数据文件。
    class TemporaryDumpDirectory
    {
    public:

        TemporaryDumpDirectory() : m_previousDirectory(std::filesystem::current_path())
        {
            const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
            m_directory = std::filesystem::temp_directory_path() / ("skiplist-dump-test-" + std::to_string(timestamp));
            if (!std::filesystem::create_directory(m_directory)) throw std::runtime_error("Failed to create dump test directory");

            try
            {
                std::filesystem::current_path(m_directory);
            }
            catch (...)
            {
                std::error_code ignoredError;
                std::filesystem::remove(m_directory, ignoredError);
                throw;
            }
        }

        ~TemporaryDumpDirectory()
        {
            std::error_code ignoredError;
            std::filesystem::current_path(m_previousDirectory, ignoredError);
            // 只移除本测试创建的三个固定路径；不递归删除其他文件。
            std::filesystem::remove(m_directory / "store" / "dumpFile", ignoredError);
            std::filesystem::remove(m_directory / "store", ignoredError);
            std::filesystem::remove(m_directory, ignoredError);
        }

        TemporaryDumpDirectory(const TemporaryDumpDirectory &) = delete;
        TemporaryDumpDirectory &operator=(const TemporaryDumpDirectory &) = delete;

    private:

        std::filesystem::path m_previousDirectory;
        std::filesystem::path m_directory;
    };

    class SkipListDumpTest : public SkipListTest
    {
    private:

        TemporaryDumpDirectory m_directory;
    };

    class SkipListLoadTest : public SkipListTest
    {
    private:

        TemporaryDumpDirectory m_directory;
    };

    void writeDumpFile(const std::string &contents)
    {
        std::filesystem::create_directories("store");
        std::ofstream file("store/dumpFile", std::ios::out | std::ios::trunc);
        if (!file.is_open()) throw std::runtime_error("Failed to open test dump file");
        file << contents;
        file.close();
        if (!file) throw std::runtime_error("Failed to write test dump file");
    }

    std::string readDumpFile()
    {
        std::ifstream file("store/dumpFile");
        EXPECT_TRUE(file.is_open());
        std::ostringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }

} // namespace


TEST_F(SkipListTest, EmptyListHasLevelZeroAndSafeMissingOperations)
{
    SkipList<int, std::string> list(4);

    EXPECT_EQ(list.size(), 0);
    EXPECT_FALSE(searchQuiet(list, 10));
    EXPECT_NO_THROW(deleteQuiet(list, 10));
    EXPECT_EQ(list.size(), 0);

    const std::string display = displayQuiet(list);
    EXPECT_NE(display.find("Level 0:"), std::string::npos);
    EXPECT_EQ(display.find("Level 1:"), std::string::npos);
    expectValidDisplayedStructure(display, {});
}

TEST_F(SkipListTest, InsertsNewKeysAndIncrementsSize)
{
    SkipList<int, std::string> list(4);
    bool insertedFive = false;
    bool insertedTwo = false;
    bool insertedNine = false;

    captureOutput([&]
                  { insertedFive = list.insertElement(5, "five"); });
    captureOutput([&]
                  { insertedTwo = list.insertElement(2, "two"); });
    captureOutput([&]
                  { insertedNine = list.insertElement(9, "nine"); });

    EXPECT_TRUE(insertedFive);
    EXPECT_TRUE(insertedTwo);
    EXPECT_TRUE(insertedNine);
    EXPECT_EQ(list.size(), 3);
    EXPECT_TRUE(searchQuiet(list, 2));
    EXPECT_TRUE(searchQuiet(list, 5));
    EXPECT_TRUE(searchQuiet(list, 9));

    const std::string display = displayQuiet(list);
    expectValidDisplayedStructure(display, {2, 5, 9});
}

TEST_F(SkipListTest, DuplicateKeyReturnsFalseAndKeepsOriginalValue)
{
    SkipList<int, std::string> list(4);
    bool firstInserted = false;
    bool duplicateInserted = true;

    captureOutput([&]
                  { firstInserted = list.insertElement(5, "original"); });
    captureOutput([&]
                  { duplicateInserted = list.insertElement(5, "replacement"); });

    EXPECT_TRUE(firstInserted);
    EXPECT_FALSE(duplicateInserted);
    EXPECT_EQ(list.size(), 1);

    const std::string searchOutput = captureOutput([&]
                                                   { EXPECT_TRUE(list.searchElement(5)); });
    EXPECT_NE(searchOutput.find("Found key: 5, value: original"), std::string::npos);
    EXPECT_EQ(searchOutput.find("replacement"), std::string::npos);
}

TEST_F(SkipListTest, FindsKeysAtBeginningMiddleAndEndAndRejectsMissingKeys)
{
    SkipList<int, std::string> list(5);
    const std::vector<std::pair<int, std::string>> entries = {
        {30, "thirty"}, {-10, "minus-ten"}, {50, "fifty"}, {0, "zero"}, {20, "twenty"}, {40, "forty"}, {10, "ten"}};

    captureOutput([&]
                  {
        for (const auto &entry : entries) EXPECT_TRUE(list.insertElement(entry.first, entry.second)); });

    EXPECT_EQ(list.size(), 7);
    EXPECT_TRUE(searchQuiet(list, -10)); // first key, including a negative key
    EXPECT_TRUE(searchQuiet(list, 20));  // middle key
    EXPECT_TRUE(searchQuiet(list, 50));  // last key
    EXPECT_TRUE(searchQuiet(list, 0));   // zero is a valid key
    EXPECT_FALSE(searchQuiet(list, -20));
    EXPECT_FALSE(searchQuiet(list, 15));
    EXPECT_FALSE(searchQuiet(list, 60));

    const std::string display = displayQuiet(list);
    expectValidDisplayedStructure(display, {-10, 0, 10, 20, 30, 40, 50});
    EXPECT_LE(parseDisplayedLevels(display).size(), 6u); // maxLevel=5 allows levels 0 through 5.
}

TEST_F(SkipListTest, KeepsCorrectOrderForAscendingAndDescendingInsertions)
{
    SkipList<int, int> ascending(4);
    SkipList<int, int> descending(4);

    captureOutput([&]
                  {
        for (int key = 1; key <= 20; ++key) EXPECT_TRUE(ascending.insertElement(key, key));
        for (int key = 20; key >= 1; --key) EXPECT_TRUE(descending.insertElement(key, key)); });

    const std::vector<int> expectedKeys = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    expectValidDisplayedStructure(displayQuiet(ascending), expectedKeys);
    expectValidDisplayedStructure(displayQuiet(descending), expectedKeys);
    EXPECT_EQ(ascending.size(), 20);
    EXPECT_EQ(descending.size(), 20);
}

TEST_F(SkipListTest, DeleteRemovesMiddleHeadTailAndMissingKeysCorrectly)
{
    SkipList<int, std::string> list(5);
    captureOutput([&]
                  {
        EXPECT_TRUE(list.insertElement(10, "ten"));
        EXPECT_TRUE(list.insertElement(20, "twenty"));
        EXPECT_TRUE(list.insertElement(30, "thirty"));
        EXPECT_TRUE(list.insertElement(40, "forty"));
        EXPECT_TRUE(list.insertElement(50, "fifty")); });

    deleteQuiet(list, 30); // middle node
    EXPECT_EQ(list.size(), 4);
    EXPECT_FALSE(searchQuiet(list, 30));

    deleteQuiet(list, 999); // absent key: no links or size should change
    EXPECT_EQ(list.size(), 4);
    EXPECT_TRUE(searchQuiet(list, 20));
    EXPECT_TRUE(searchQuiet(list, 40));

    deleteQuiet(list, 10); // first node
    deleteQuiet(list, 50); // last node
    EXPECT_EQ(list.size(), 2);
    EXPECT_FALSE(searchQuiet(list, 10));
    EXPECT_FALSE(searchQuiet(list, 50));
    expectValidDisplayedStructure(displayQuiet(list), {20, 40});
}

TEST_F(SkipListTest, DeletingEveryNodeShrinksToLevelZeroAndListCanBeReused)
{
    SkipList<int, std::string> list(5);
    captureOutput([&]
                  {
        for (int key = 0; key < 40; ++key) EXPECT_TRUE(list.insertElement(key, std::to_string(key))); });
    EXPECT_EQ(list.size(), 40);

    captureOutput([&]
                  {
        for (int key = 0; key < 40; ++key) list.deleteElement(key); });

    EXPECT_EQ(list.size(), 0);
    EXPECT_FALSE(searchQuiet(list, 0));
    const std::string emptyDisplay = displayQuiet(list);
    EXPECT_NE(emptyDisplay.find("Level 0:"), std::string::npos);
    EXPECT_EQ(emptyDisplay.find("Level 1:"), std::string::npos);
    expectValidDisplayedStructure(emptyDisplay, {});

    bool insertedAfterClear = false;
    captureOutput([&]
                  { insertedAfterClear = list.insertElement(25, "twenty-five"); });
    EXPECT_TRUE(insertedAfterClear);
    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(searchQuiet(list, 25));
    expectValidDisplayedStructure(displayQuiet(list), {25});
}

TEST_F(SkipListTest, MaximumLevelZeroOnlyUsesBottomLayer)
{
    SkipList<int, std::string> list(0);
    captureOutput([&]
                  {
        EXPECT_TRUE(list.insertElement(1, "one"));
        EXPECT_TRUE(list.insertElement(2, "two"));
        EXPECT_TRUE(list.insertElement(3, "three")); });

    const std::string display = displayQuiet(list);
    EXPECT_EQ(parseDisplayedLevels(display).size(), 1u);
    EXPECT_NE(display.find("Level 0:"), std::string::npos);
    EXPECT_EQ(display.find("Level 1:"), std::string::npos);
    expectValidDisplayedStructure(display, {1, 2, 3});

    EXPECT_TRUE(searchQuiet(list, 1));
    EXPECT_TRUE(searchQuiet(list, 3));
    deleteQuiet(list, 2);
    EXPECT_EQ(list.size(), 2);
    EXPECT_FALSE(searchQuiet(list, 2));
    expectValidDisplayedStructure(displayQuiet(list), {1, 3});
}

TEST_F(SkipListTest, SameSeedProducesTheSameLevelStructure)
{
    EXPECT_EQ(structureForSeed(9876), structureForSeed(9876));
}

TEST_F(SkipListTest, HighestActiveLevelShrinksAfterItsNodesAreDeleted)
{
    bool foundStructureWithUpperLayers = false;
    bool allUpperLayersWereRemoved = false;

    // Probe a small range of fixed seeds instead of assuming one platform's rand() sequence.
    for (unsigned int seed = 1; seed <= 64 && !foundStructureWithUpperLayers; ++seed)
    {
        std::srand(seed);
        SkipList<int, int> list(6);
        captureOutput([&]
                      {
            for (int key = 0; key < 40; ++key) list.insertElement(key, key); });

        const std::string beforeDeletion = displayQuiet(list);
        if (parseDisplayedLevels(beforeDeletion).size() < 2) continue;

        foundStructureWithUpperLayers = true;
        expectValidDisplayedStructure(beforeDeletion, [&]
                                      {
            std::vector<int> keys;
            for (int key = 0; key < 40; ++key) keys.push_back(key);
            return keys; }());

        captureOutput([&]
                      {
            for (int key = 0; key < 40; ++key) list.deleteElement(key); });
        const std::string afterDeletion = displayQuiet(list);
        allUpperLayersWereRemoved = parseDisplayedLevels(afterDeletion).size() == 1 && list.size() == 0;
        expectValidDisplayedStructure(afterDeletion, {});
    }

    EXPECT_TRUE(foundStructureWithUpperLayers)
        << "none of the probed seeds generated an upper level";
    EXPECT_TRUE(allUpperLayersWereRemoved)
        << "emptying the list should leave only level 0";
}

TEST_F(SkipListTest, ConcurrentReadersAndWritersKeepSizeAndAllInsertedKeysConsistent)
{
    constexpr int writerCount = 4;
    constexpr int readerCount = 3;
    constexpr int valuesPerWriter = 30;
    constexpr int expectedSize = writerCount * valuesPerWriter;

    SkipList<int, std::string> list(6);
    std::atomic<bool> start{false};
    std::atomic<int> duplicateInsertions{0};
    std::atomic<int> invalidSizeReads{0};
    std::vector<std::thread> threads;

    {
        // 丢弃并发日志；读线程检查 size()，断言在线程全部结束后执行。
        CoutDiscard discardedOutput;

        for (int reader = 0; reader < readerCount; ++reader)
        {
            threads.emplace_back([&]
                                 {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();

                int previousSize = 0;
                for (int sample = 0; sample < 2000; ++sample)
                {
                    const int currentSize = list.size();
                    if (currentSize < previousSize || currentSize > expectedSize)
                    {
                        invalidSizeReads.fetch_add(1, std::memory_order_relaxed);
                    }
                    previousSize = currentSize;
                } });
        }

        for (int writer = 0; writer < writerCount; ++writer)
        {
            threads.emplace_back([&, writer]
                                 {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();

                for (int offset = 0; offset < valuesPerWriter; ++offset)
                {
                    const int key = writer * valuesPerWriter + offset;
                    if (!list.insertElement(key, std::to_string(key)))
                    {
                        duplicateInsertions.fetch_add(1, std::memory_order_relaxed);
                    }
                } });
        }

        start.store(true, std::memory_order_release);
        for (std::thread &thread : threads) thread.join();
    }

    EXPECT_EQ(list.size(), expectedSize);
    EXPECT_EQ(duplicateInsertions.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(invalidSizeReads.load(std::memory_order_relaxed), 0);

    bool everyKeyExists = true;
    captureOutput([&]
                  {
        for (int key = 0; key < expectedSize; ++key)
        {
            if (!list.searchElement(key)) everyKeyExists = false;
        } });
    EXPECT_TRUE(everyKeyExists);
    expectValidDisplayedStructure(displayQuiet(list), [&]
                                  {
        std::vector<int> keys;
        for (int key = 0; key < expectedSize; ++key) keys.push_back(key);
        return keys; }());
}

TEST_F(SkipListTest, ConcurrentInsertionOfTheSameKeySucceedsExactlyOnce)
{
    constexpr int writerCount = 8;
    SkipList<int, std::string> list(4);
    std::atomic<bool> start{false};
    std::atomic<int> successfulInsertions{0};
    std::vector<std::thread> writers;

    {
        // 同时启动多个线程竞争同一个 key，日志通过无状态缓冲区丢弃。
        CoutDiscard discardedOutput;
        for (int writer = 0; writer < writerCount; ++writer)
        {
            writers.emplace_back([&, writer]
                                 {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                if (list.insertElement(7, "candidate-" + std::to_string(writer)))
                {
                    successfulInsertions.fetch_add(1, std::memory_order_relaxed);
                } });
        }
        start.store(true, std::memory_order_release);
        for (std::thread &thread : writers) thread.join();
    }

    EXPECT_EQ(successfulInsertions.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(list.size(), 1);

    const std::string searchOutput = captureOutput([&]
                                                   { EXPECT_TRUE(list.searchElement(7)); });
    EXPECT_NE(searchOutput.find("Found key: 7, value: candidate-"), std::string::npos);
    expectValidDisplayedStructure(displayQuiet(list), {7});
}


TEST_F(SkipListDumpTest, SavesAllPairsInKeyOrderWithoutChangingTheList)
{
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  {
        ASSERT_TRUE(list.insertElement(7, "seven:tail"));
        ASSERT_TRUE(list.insertElement(1, "one"));
        ASSERT_TRUE(list.insertElement(3, "")); });
    const std::string beforeSave = displayQuiet(list);

    EXPECT_NO_THROW(captureOutput([&]
                                  { list.dumpFile(); }));

    EXPECT_EQ(readDumpFile(), "1:one\n3:\n7:seven:tail\n");
    EXPECT_EQ(list.size(), 3);
    EXPECT_TRUE(searchQuiet(list, 1));
    EXPECT_TRUE(searchQuiet(list, 3));
    EXPECT_TRUE(searchQuiet(list, 7));
    EXPECT_EQ(displayQuiet(list), beforeSave);
}

TEST_F(SkipListDumpTest, RepeatedSavesOverwriteOldDataAndEmptyListProducesAnEmptyFile)
{
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  {
        ASSERT_TRUE(list.insertElement(1, "one"));
        ASSERT_TRUE(list.insertElement(2, "two"));
        list.dumpFile(); });
    EXPECT_EQ(readDumpFile(), "1:one\n2:two\n");

    captureOutput([&]
                  {
        list.deleteElement(1);
        ASSERT_TRUE(list.insertElement(3, "three"));
        list.dumpFile(); });
    EXPECT_EQ(readDumpFile(), "2:two\n3:three\n");

    captureOutput([&]
                  {
        list.deleteElement(2);
        list.deleteElement(3);
        list.dumpFile(); });
    EXPECT_EQ(list.size(), 0);
    EXPECT_EQ(readDumpFile(), "");
}

TEST_F(SkipListDumpTest, OpeningAnInvalidDestinationThrowsAndLeavesTheListUsable)
{
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  { ASSERT_TRUE(list.insertElement(1, "one")); });
    // 用目录占据目标文件路径，确保打开输出文件失败。
    std::filesystem::create_directories("store/dumpFile");

    EXPECT_THROW(captureOutput([&]
                               { list.dumpFile(); }),
                 std::runtime_error);

    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(searchQuiet(list, 1)); // 异常路径也必须释放锁。
}

TEST_F(SkipListDumpTest, ConcurrentSavesOfTheSameListProduceOneCompleteSnapshot)
{
    SkipList<int, std::string> list(4);
    std::string expectedContents;
    captureOutput([&]
                  {
        for (int key = 0; key < 50; ++key)
        {
            ASSERT_TRUE(list.insertElement(key, std::to_string(key)));
            expectedContents += std::to_string(key) + ":" + std::to_string(key) + "\n";
        } });

    std::atomic<bool> start{false};
    std::atomic<int> failures{0};
    std::vector<std::thread> writers;
    {
        CoutDiscard discardedOutput;
        for (int writer = 0; writer < 4; ++writer)
        {
            writers.emplace_back([&]
                                 {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                try
                {
                    list.dumpFile();
                }
                catch (...)
                {
                    failures.fetch_add(1, std::memory_order_relaxed);
                } });
        }
        start.store(true, std::memory_order_release);
        for (std::thread &writer : writers) writer.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(readDumpFile(), expectedContents);
    EXPECT_EQ(list.size(), 50);
}


TEST_F(SkipListLoadTest, SavesLoadsAndKeepsExistingValueForDuplicateKey)
{
    SkipList<int, std::string> source(4);
    captureOutput([&]
                  {
        EXPECT_TRUE(source.insertElement(1, "one"));
        EXPECT_TRUE(source.insertElement(3, "three words: tail"));
        source.dumpFile(); });
    EXPECT_EQ(readDumpFile(), "1:one\n3:three words: tail\n");

    SkipList<int, std::string> restored(4);
    captureOutput([&]
                  {
        EXPECT_TRUE(restored.insertElement(1, "original"));
        restored.loadFile(); });

    EXPECT_EQ(restored.size(), 2);
    const std::string output = captureOutput([&]
                                             {
        EXPECT_TRUE(restored.searchElement(1));
        EXPECT_TRUE(restored.searchElement(3)); });
    EXPECT_NE(output.find("Found key: 1, value: original"), std::string::npos);
    EXPECT_NE(output.find("Found key: 3, value: three words: tail"), std::string::npos);

    captureOutput([&]
                  { restored.loadFile(); });
    EXPECT_EQ(restored.size(), 2);
}


TEST(NodeTest, InitializesKeyValueAndForwardSlotsForEveryParticipatingLevel)
{
    for (int level : {0, 1, 4})
    {
        SCOPED_TRACE(level);
        Node<int, std::string> node(7, "seven", level);
        EXPECT_EQ(node.getKey(), 7);
        EXPECT_EQ(node.getValue(), "seven");
        EXPECT_EQ(node.m_nodeLevel, level);
        EXPECT_EQ(node.m_forward.size(), static_cast<size_t>(level + 1));
        EXPECT_TRUE(std::all_of(node.m_forward.begin(), node.m_forward.end(),
                                [](const Node<int, std::string> *next)
                                { return next == nullptr; }));
    }
}

TEST(NodeTest, SetValueKeepsKeyLevelAndLinksUnchanged)
{
    Node<int, std::string> node(7, "original", 1);
    Node<int, std::string> next(9, "nine", 1);
    node.m_forward[0] = &next;
    node.m_forward[1] = &next;

    node.setValue("replacement");

    EXPECT_EQ(node.getValue(), "replacement");
    EXPECT_EQ(node.getKey(), 7);
    EXPECT_EQ(node.m_nodeLevel, 1);
    EXPECT_EQ(node.m_forward[0], &next);
    EXPECT_EQ(node.m_forward[1], &next);
}

TEST_F(SkipListTest, SupportsMinimumAndMaximumIntKeys)
{
    const int minimum = std::numeric_limits<int>::min();
    const int maximum = std::numeric_limits<int>::max();
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  {
        EXPECT_TRUE(list.insertElement(maximum, "maximum"));
        EXPECT_TRUE(list.insertElement(0, "zero"));
        EXPECT_TRUE(list.insertElement(minimum, "minimum")); });

    EXPECT_EQ(list.size(), 3);
    EXPECT_TRUE(searchQuiet(list, minimum));
    EXPECT_TRUE(searchQuiet(list, maximum));
    expectValidDisplayedStructure(displayQuiet(list), {minimum, 0, maximum});

    deleteQuiet(list, minimum);
    deleteQuiet(list, maximum);
    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(searchQuiet(list, 0));
    EXPECT_FALSE(searchQuiet(list, minimum));
    EXPECT_FALSE(searchQuiet(list, maximum));
}

TEST_F(SkipListTest, RepeatedDeletionOfTheLastKeyDoesNotUnderflowSizeAndKeyCanBeReinserted)
{
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  { EXPECT_TRUE(list.insertElement(7, "original")); });
    deleteQuiet(list, 7);
    deleteQuiet(list, 7);
    EXPECT_EQ(list.size(), 0);
    EXPECT_FALSE(searchQuiet(list, 7));

    captureOutput([&]
                  { EXPECT_TRUE(list.insertElement(7, "replacement")); });
    EXPECT_EQ(list.size(), 1);
    const std::string output = captureOutput([&]
                                             { EXPECT_TRUE(list.searchElement(7)); });
    EXPECT_NE(output.find("Found key: 7, value: replacement"), std::string::npos);
}

TEST_F(SkipListTest, MixedOperationsMatchAnIndependentMap)
{
    SkipList<int, std::string> list(6);
    std::map<int, std::string> expected;
    std::mt19937 operations(2026); // 只用于生成可复现的测试操作，不改变生产代码的随机数实现。
    CoutCapture capturedOutput;

    for (int step = 0; step < 400; ++step)
    {
        SCOPED_TRACE(step);
        const int key = static_cast<int>(operations() % 61) - 30;
        const int action = static_cast<int>(operations() % 3);
        if (action == 0)
        {
            const std::string value = "value-" + std::to_string(step);
            const bool expectedInsertion = expected.emplace(key, value).second;
            EXPECT_EQ(list.insertElement(key, value), expectedInsertion);
        }
        else if (action == 1)
        {
            expected.erase(key);
            list.deleteElement(key);
        }
        else
        {
            EXPECT_EQ(list.searchElement(key), expected.find(key) != expected.end());
        }
        EXPECT_EQ(list.size(), static_cast<int>(expected.size()));

        if (step % 25 == 0 || step == 399)
        {
            std::vector<int> expectedKeys;
            std::ostringstream expectedBottom;
            expectedBottom << "Level 0: ";
            for (const auto &entry : expected)
            {
                expectedKeys.push_back(entry.first);
                expectedBottom << entry.first << ":" << entry.second << ";";
            }
            expectedBottom << '\n';
            const std::string display = displayQuiet(list);
            expectValidDisplayedStructure(display, expectedKeys);
            EXPECT_NE(display.find(expectedBottom.str()), std::string::npos);
        }
    }

    for (int key = -30; key <= 30; ++key)
    {
        EXPECT_EQ(list.searchElement(key), expected.find(key) != expected.end()) << "key " << key;
    }
}

TEST_F(SkipListTest, DeletionAndDestructionReleaseEveryStoredValue)
{
    constexpr int count = 24;
    std::vector<std::weak_ptr<int>> values;
    {
        SkipList<int, std::shared_ptr<int>> list(4);
        captureOutput([&]
                      {
            for (int key = 0; key < count; ++key)
            {
                auto value = std::make_shared<int>(key);
                values.push_back(value);
                EXPECT_TRUE(list.insertElement(key, value));
            } });
        for (int key = 0; key < count; key += 2) deleteQuiet(list, key);
        EXPECT_EQ(list.size(), count / 2);
        for (int key = 0; key < count; ++key)
        {
            EXPECT_EQ(values[key].expired(), key % 2 == 0) << "key " << key;
        }
    }
    // 不再有其他 shared_ptr 持有 value；节点若未释放，这些 weak_ptr 就不会过期。
    for (const auto &value : values) EXPECT_TRUE(value.expired());
}

TEST_F(SkipListTest, RemovingHighestLayerNodesShrinksHeightAndPreservesOtherNodes)
{
    for (unsigned int seed = 1; seed <= 64; ++seed)
    {
        std::srand(seed);
        SkipList<int, int> list(6);
        captureOutput([&]
                      {
            for (int key = 0; key < 40; ++key) EXPECT_TRUE(list.insertElement(key, key)); });
        const auto before = parseDisplayedLevels(displayQuiet(list));
        if (before.size() < 2 || before.back().size() == before.front().size()) continue;

        const auto highestKeys = before.back();
        std::vector<int> remainingKeys;
        for (int key = 0; key < 40; ++key)
        {
            if (std::find(highestKeys.begin(), highestKeys.end(), key) == highestKeys.end()) remainingKeys.push_back(key);
        }
        for (int key : highestKeys) deleteQuiet(list, key);

        const std::string afterDisplay = displayQuiet(list);
        const auto after = parseDisplayedLevels(afterDisplay);
        ASSERT_FALSE(after.empty());
        EXPECT_LT(after.size(), before.size());
        EXPECT_EQ(list.size(), static_cast<int>(remainingKeys.size()));
        expectValidDisplayedStructure(afterDisplay, remainingKeys);
        for (int key : highestKeys) EXPECT_FALSE(searchQuiet(list, key));
        for (int key : remainingKeys) EXPECT_TRUE(searchQuiet(list, key));
        return;
    }
    FAIL() << "none of the probed seeds generated a suitable highest layer";
}

TEST_F(SkipListTest, ConcurrentSearchInsertAndDeletePreserveStableKeysAndFinalContents)
{
    constexpr int stableCount = 8;
    constexpr int writerCount = 2;
    constexpr int valuesPerWriter = 8;
    constexpr int expectedSize = stableCount + writerCount * valuesPerWriter;
    SkipList<int, std::string> list(5);
    captureOutput([&]
                  {
        for (int key = 0; key < stableCount; ++key) EXPECT_TRUE(list.insertElement(key, "stable")); });

    std::atomic<bool> start{false};
    std::atomic<int> writersRemaining{writerCount};
    std::atomic<int> failedInsertions{0};
    std::atomic<int> missingStableKeys{0};
    std::atomic<int> invalidSizes{0};
    std::vector<std::thread> threads;
    {
        CoutDiscard discardedOutput;
        for (int reader = 0; reader < 2; ++reader)
        {
            threads.emplace_back([&]
                                 {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                for (int sample = 0; sample < 200 || writersRemaining.load(std::memory_order_acquire) > 0; ++sample)
                {
                    if (!list.searchElement(sample % stableCount)) missingStableKeys.fetch_add(1, std::memory_order_relaxed);
                    // 这些 key 正在被增删，查询结果可能为 true 或 false；查询过程必须保持安全。
                    list.searchElement(stableCount + sample % (writerCount * valuesPerWriter));
                    const int currentSize = list.size();
                    if (currentSize < stableCount || currentSize > expectedSize) invalidSizes.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                } });
        }
        for (int writer = 0; writer < writerCount; ++writer)
        {
            threads.emplace_back([&, writer]
                                 {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                const int firstKey = stableCount + writer * valuesPerWriter;
                for (int round = 0; round < 4; ++round)
                {
                    for (int key = firstKey; key < firstKey + valuesPerWriter; ++key)
                    {
                        if (!list.insertElement(key, "changing")) failedInsertions.fetch_add(1, std::memory_order_relaxed);
                    }
                    for (int key = firstKey; key < firstKey + valuesPerWriter; ++key) list.deleteElement(key);
                }
                for (int key = firstKey; key < firstKey + valuesPerWriter; ++key)
                {
                    if (!list.insertElement(key, "final")) failedInsertions.fetch_add(1, std::memory_order_relaxed);
                }
                writersRemaining.fetch_sub(1, std::memory_order_release); });
        }
        start.store(true, std::memory_order_release);
        for (auto &thread : threads) thread.join();
    }

    EXPECT_EQ(failedInsertions.load(), 0);
    EXPECT_EQ(missingStableKeys.load(), 0);
    EXPECT_EQ(invalidSizes.load(), 0);
    EXPECT_EQ(list.size(), expectedSize);
    std::vector<int> expectedKeys;
    for (int key = 0; key < expectedSize; ++key)
    {
        EXPECT_TRUE(searchQuiet(list, key));
        expectedKeys.push_back(key);
    }
    expectValidDisplayedStructure(displayQuiet(list), expectedKeys);
}

TEST_F(SkipListDumpTest, DirectoryCreationFailureReleasesLockAndKeepsData)
{
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  { EXPECT_TRUE(list.insertElement(1, "one")); });
    std::ofstream blocker("store");
    ASSERT_TRUE(blocker.is_open());
    blocker.close(); // 用普通文件占据 store 目录的位置。

    EXPECT_THROW(captureOutput([&]
                               { list.dumpFile(); }),
                 std::filesystem::filesystem_error);

    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(searchQuiet(list, 1));
}

TEST_F(SkipListLoadTest, LoadsIntoAnEmptyListIncludingZeroAndIntBoundaryKeys)
{
    const int minimum = std::numeric_limits<int>::min();
    const int maximum = std::numeric_limits<int>::max();
    writeDumpFile(std::to_string(maximum) + ":maximum\n0:zero\n" + std::to_string(minimum) + ":minimum");
    SkipList<int, std::string> list(4);

    captureOutput([&]
                  { list.loadFile(); });

    EXPECT_EQ(list.size(), 3);
    EXPECT_TRUE(searchQuiet(list, minimum));
    EXPECT_TRUE(searchQuiet(list, maximum));
    EXPECT_TRUE(searchQuiet(list, 0));
    expectValidDisplayedStructure(displayQuiet(list), {minimum, 0, maximum});
}

TEST_F(SkipListLoadTest, EmptyFileKeepsExistingData)
{
    writeDumpFile("");
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  { EXPECT_TRUE(list.insertElement(9, "nine")); });

    EXPECT_NO_THROW(captureOutput([&]
                                  { list.loadFile(); }));

    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(searchQuiet(list, 9));
}

TEST_F(SkipListLoadTest, SkipsBlankLinesMissingDelimitersAndEmptyKeysOrValues)
{
    writeDumpFile("\nno delimiter\n:missing-key\n2:\n:\n1:first\n\n3:third:tail\n");
    SkipList<int, std::string> list(4);

    EXPECT_NO_THROW(captureOutput([&]
                                  { list.loadFile(); }));

    EXPECT_EQ(list.size(), 2);
    expectValidDisplayedStructure(displayQuiet(list), {1, 3});
    EXPECT_FALSE(searchQuiet(list, 2)); // 当前参考式实现明确跳过空 value。
    const std::string output = captureOutput([&]
                                             { EXPECT_TRUE(list.searchElement(3)); });
    EXPECT_NE(output.find("Found key: 3, value: third:tail"), std::string::npos);
}

TEST_F(SkipListLoadTest, DuplicateKeysInsideFileKeepFirstLoadedValueAndPreserveOtherExistingKeys)
{
    const std::string contents = "2:pear\n1:apple\n2:peach\n1:banana\n";
    writeDumpFile(contents);
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  {
        EXPECT_TRUE(list.insertElement(9, "nine"));
        list.loadFile(); });

    EXPECT_EQ(list.size(), 3);
    expectValidDisplayedStructure(displayQuiet(list), {1, 2, 9});
    const std::string output = captureOutput([&]
                                             {
        EXPECT_TRUE(list.searchElement(1));
        EXPECT_TRUE(list.searchElement(2));
        EXPECT_TRUE(list.searchElement(9)); });
    EXPECT_NE(output.find("Found key: 1, value: apple"), std::string::npos);
    EXPECT_NE(output.find("Found key: 2, value: pear"), std::string::npos);
    EXPECT_NE(output.find("Found key: 9, value: nine"), std::string::npos);
    EXPECT_EQ(readDumpFile(), contents);
}

TEST_F(SkipListLoadTest, MissingFileThrowsAndKeepsExistingData)
{
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  { EXPECT_TRUE(list.insertElement(9, "nine")); });
    ASSERT_FALSE(std::filesystem::exists("store/dumpFile"));

    EXPECT_THROW(captureOutput([&]
                               { list.loadFile(); }),
                 std::runtime_error);

    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(searchQuiet(list, 9));
    EXPECT_FALSE(std::filesystem::exists("store/dumpFile"));
}

TEST_F(SkipListLoadTest, InvalidIntegerKeyThrowsAfterKeepingPreviouslyLoadedRecords)
{
    writeDumpFile("1:one\nnot-a-number:invalid\n3:three\n");
    SkipList<int, std::string> list(4);
    captureOutput([&]
                  { EXPECT_TRUE(list.insertElement(9, "nine")); });

    EXPECT_THROW(captureOutput([&]
                               { list.loadFile(); }),
                 std::invalid_argument);

    EXPECT_EQ(list.size(), 2);
    EXPECT_TRUE(searchQuiet(list, 1));
    EXPECT_TRUE(searchQuiet(list, 9));
    EXPECT_FALSE(searchQuiet(list, 3));
    writeDumpFile("2:two\n");
    EXPECT_NO_THROW(captureOutput([&]
                                  { list.loadFile(); }));
    EXPECT_EQ(list.size(), 3);
    EXPECT_TRUE(searchQuiet(list, 2));
}

TEST_F(SkipListLoadTest, IntegerOverflowThrowsAfterKeepingPreviouslyLoadedRecords)
{
    writeDumpFile("1:one\n999999999999999999999999999999:overflow\n3:three\n");
    SkipList<int, std::string> list(4);

    EXPECT_THROW(captureOutput([&]
                               { list.loadFile(); }),
                 std::out_of_range);

    EXPECT_EQ(list.size(), 1);
    EXPECT_TRUE(searchQuiet(list, 1));
    EXPECT_FALSE(searchQuiet(list, 3));
}


int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);


    return RUN_ALL_TESTS();
}
