#ifndef _SKIPLIST_SKIPLIST_H_
#define _SKIPLIST_SKIPLIST_H_

#include "node.h"

#include <iostream>


template <typename K, typename T>
class SkipList
{

public:

    // maxLevel 是允许的最高层编号，不是层数；例如 maxLevel = 4 时会有第 0 到第 4 层，共 5 层。
    explicit SkipList(int maxLevel) : m_maxLevel(maxLevel), m_header(new Node<K, T>(K{}, T{}, maxLevel)) {}

    ~SkipList();

    // 插入键值对。返回 true 表示插入成功，返回 false 表示 key 已存在。
    bool insertElement(const K &key, const T &value);

    bool searchElement(const K &key) const;

    void deleteElement(const K &key);

    void displayList() const;

    int size() const { return m_size; }


private:

    // 为新节点随机选择最高层编号；新节点会参与从第 0 层到该层的所有层。
    int getRandomLevel();

    // 释放从 node 开始的底层链表节点。
    void clear(Node<K, T> *node);


    // 当前跳表允许到达的最高层编号。层编号从 0 开始，因此 maxLevel = 4 时，编号范围为 0 到 4。头节点需要 maxLevel + 1 个 forward 槽位。
    int m_maxLevel;

    // 当前跳表实际使用的最高层编号，范围为 0 到 m_maxLevel。高于该编号的层暂时为空；遍历跳表从第 m_skipListLevel 层开始。
    int m_skipListLevel = 0;

    Node<K, T> *m_header = nullptr;

    int m_size = 0;
};


template <typename K, typename T>
SkipList<K, T>::~SkipList()
{
    if (!m_header) return;

    // 从头节点的第 0 层后继开始清理；头节点最后单独释放。
    if (!m_header->m_forward.empty()) clear(m_header->m_forward[0]);

    delete m_header;
    m_header = nullptr;
}

// Insert given key and value in skip list
/*
                           +------------+
                           |  insert 50 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |                      insert +----+
level 3         1+-------->10+---------------> | 50 |          70       100
                                               |    |
                                               |    |
level 2         1          10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 1         1    4     10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 0         1    4   9 10         30   40  | 50 |  60      70       100
                                               +----+
*/

template <typename K, typename T>
bool SkipList<K, T>::insertElement(const K &key, const T &value)
{
    Node<K, T> *current = m_header;

    // update[i] 保存第 i 层中，新节点插入位置的前驱。后面要修改这些前驱的 m_forward[i]，把新节点接入对应层的链表。
    std::vector<Node<K, T> *> update(1 + m_maxLevel, nullptr);

    // 同 searchElement() 逻辑，从当前实际使用的最高层向下查找。每层向右越过所有小于 key 的节点，停下来的 current 就是这一层插入位置的前驱。
    for (int i = m_skipListLevel; i >= 0; --i)
    {
        while (current->m_forward[i] && current->m_forward[i]->getKey() < key) current = current->m_forward[i];

        update[i] = current;
    }

    // 因此检查底层（第 0 层）插入位置的后继，判断 key 是否已存在。
    current = update[0]->m_forward[0];
    if (current && key == current->getKey())
    {
        std::cout << "key: " << key << ", exists" << std::endl;
        return false;
    }

    // 随机决定新节点参与的最高层编号；新节点会出现在第 0 层到 randomLevel 层。
    int randomLevel = getRandomLevel();

    // 若新节点会把跳表顶层抬高，超出的层目前没有数据节点，前驱都是头节点，前面通过 m_skipListLevel 遍历无法填充这些层的 update[i] 信息。因此这里先填写 update；等节点成功接入后再更新 m_skipListLevel。
    if (randomLevel > m_skipListLevel)
    {
        for (int i = 1 + m_skipListLevel; i <= randomLevel; ++i) update[i] = m_header;
    }

    Node<K, T> *newNode = new Node<K, T>(key, value, randomLevel);

    // 逐层插入：先让新节点指向原后继，再让前驱指向新节点，避免丢失原链条。
    for (int i = 0; i <= randomLevel; ++i)
    {
        newNode->m_forward[i] = update[i]->m_forward[i];
        update[i]->m_forward[i] = newNode;
    }

    // 更新状态。
    if (randomLevel > m_skipListLevel) m_skipListLevel = randomLevel;

    ++m_size;

    std::cout << "Successfully inserted key: " << key << ", value: " << value << std::endl;


    return true;
}

// Search for element in skip list
/*
                           +------------+
                           |  select 60 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |
level 3         1+-------->10+------------------>50+           70       100
                                                   |
                                                   |
level 2         1          10         30         50|           70       100
                                                   |
                                                   |
level 1         1    4     10         30         50|           70       100
                                                   |
                                                   |
level 0         1    4   9 10         30   40    50+-->60      70       100
*/

template <typename K, typename T>
bool SkipList<K, T>::searchElement(const K &key) const
{
    std::cout << "searchElement-----------------" << std::endl;

    Node<K, T> *current = m_header;

    // 从当前实际使用的最高层（m_skipListLevel）向下查找。每层只越过小于 key 的节点，停在 key 的前驱处。降到下一层时保留 current，因为它在更低层也有对应的后继指针。
    for (int i = m_skipListLevel; i >= 0; --i)
    {
        // 注意一个特殊情况，若 key == current->m_forward[i]->getKey()，此时依然会停在 key 的前驱，最底层最后判断的时候依然是成立的，符合算法模板本身。
        while (current->m_forward[i] && current->m_forward[i]->getKey() < key) current = current->m_forward[i];
    }

    // 此时 current 是第 0 层中目标位置的前驱；向右一步取得候选节点。
    current = current->m_forward[0];

    // 只有候选节点的 key 与目标相等，才表示查找成功。
    if (current && key == current->getKey())
    {
        std::cout << "Found key: " << key << ", value: " << current->getValue() << std::endl;
        return true;
    }
    else
    {
        std::cout << "Not Found Key:" << key << std::endl;
        return false;
    }
}

template <typename K, typename T>
void SkipList<K, T>::deleteElement(const K &)
{
}

template <typename K, typename T>
void SkipList<K, T>::displayList() const
{
    std::cout << "\n*****Skip List*****\n";

    for (int i = 0; i <= m_skipListLevel; i++)
    {
        std::cout << "Level " << i << ": ";

        Node<K, T> *node = m_header->m_forward[i];
        while (node)
        {
            std::cout << node->getKey() << ":" << node->getValue() << ";";
            node = node->m_forward[i];
        }

        std::cout << std::endl;
    }
}

template <typename K, typename T>
int SkipList<K, T>::getRandomLevel()
{
    // 从第 0 层开始；每次的随机数最低位为 1 就再提升一层，为 0 就停止，随机得到新节点的最高层编号。level < m_maxLevel 确保这个编号不会超过允许的最高层编号；节点会参与第 0 层到 level 层。
    int level = 0;
    while (level < m_maxLevel && (std::rand() & 1)) ++level;


    return level;
}

template <typename K, typename T>
void SkipList<K, T>::clear(Node<K, T> *node)
{
    if (!node) return;

    // 沿 m_forward[0] 遍历，因为最底层串起了所有数据节点；递归会先走到尾部再返回。返回时对当前节点执行 delete，逐个销毁所有数据节点。
    if (!node->m_forward.empty()) clear(node->m_forward[0]);

    delete node;
}


#endif
