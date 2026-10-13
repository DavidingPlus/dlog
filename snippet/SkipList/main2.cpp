#include "skiplist.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace
{

    // SkipList 当前会向 std::cout 打印操作日志；并发写测试中，输出调用被该对象的独占锁串行化。
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

            const std::size_t contentStart = line.find(": ");
            if (contentStart == std::string::npos) continue;

            std::vector<int> keys;
            std::size_t itemStart = contentStart + 2;
            while (itemStart < line.size())
            {
                const std::size_t itemEnd = line.find(';', itemStart);
                const std::size_t keyEnd = line.find(':', itemStart);
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

        for (std::size_t level = 0; level < levels.size(); ++level)
        {
            EXPECT_TRUE(std::is_sorted(levels[level].begin(), levels[level].end()))
                << "keys are not sorted at level " << level;
            EXPECT_TRUE(std::adjacent_find(levels[level].begin(), levels[level].end()) == levels[level].end())
                << "duplicate key found at level " << level;

            for (int key : levels[level])
            {
                for (std::size_t lowerLevel = 0; lowerLevel < level; ++lowerLevel)
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
        // Writers are serialized by SkipList's exclusive lock. Readers call size(), which emits no output.
        CoutCapture capturedOutput;

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
    std::atomic<int> successfulInsertions{0};
    std::vector<std::thread> writers;

    {
        // insertElement uses an exclusive lock, so its log writes are serialized during capture.
        CoutCapture capturedOutput;
        for (int writer = 0; writer < writerCount; ++writer)
        {
            writers.emplace_back([&, writer]
                                 {
                if (list.insertElement(7, "candidate-" + std::to_string(writer)))
                {
                    successfulInsertions.fetch_add(1, std::memory_order_relaxed);
                } });
        }
        for (std::thread &thread : writers) thread.join();
    }

    EXPECT_EQ(successfulInsertions.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(list.size(), 1);

    const std::string searchOutput = captureOutput([&]
                                                   { EXPECT_TRUE(list.searchElement(7)); });
    EXPECT_NE(searchOutput.find("Found key: 7, value: candidate-"), std::string::npos);
    expectValidDisplayedStructure(displayQuiet(list), {7});
}


int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);


    return RUN_ALL_TESTS();
}
