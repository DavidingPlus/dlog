#ifndef _SKIPLIST_SKIPLIST_H_
#define _SKIPLIST_SKIPLIST_H_

#include "node.h"

#include <iostream>


template <typename K, typename T>
class SkipList
{

public:

    explicit SkipList(int maxLevel) : m_maxLevel(maxLevel), m_header(new Node<K, T>(K{}, T{}, maxLevel)) {}

    ~SkipList();

    // 插入键值对：返回 0 表示成功，返回 1 表示 key 已存在。
    int insertElement(const K &key, const T &value);

    bool searchElement(const K &key) const;

    void deleteElement(const K &key);

    void displayList() const;

    int size() const { return m_elementCount; }


private:

    // 为新节点随机选择层数，并创建对应高度的节点。
    int getRandomLevel();

    Node<K, T> *createNode(const K &key, const T &value, int level);

    // 释放从 node 开始的底层链表节点。
    void clear(Node<K, T> *node);


    int m_maxLevel;

    int m_currentLevel = 0;

    Node<K, T> *m_header = nullptr;

    int m_elementCount = 0;
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

template <typename K, typename T>
int SkipList<K, T>::insertElement(const K &, const T &)
{
    return 0;
}

template <typename K, typename T>
bool SkipList<K, T>::searchElement(const K &) const
{
    return false;
}

template <typename K, typename T>
void SkipList<K, T>::deleteElement(const K &)
{
}

template <typename K, typename T>
void SkipList<K, T>::displayList() const
{
    std::cout << "\n*****Skip List*****\n";

    for (int i = 0; i <= m_currentLevel; i++)
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
    // 从第 0 层开始；每次的随机数最低位为 1 就再提升一层，为 0 就停止，用于为新节点随机选择最高层数。同时 level < m_maxLevel 确保不会超过允许的最高层编号。
    int level = 0;
    while (level < m_maxLevel && (std::rand() & 1)) ++level;


    return level;
}

template <typename K, typename T>
Node<K, T> *SkipList<K, T>::createNode(const K &, const T &, int)
{
    return nullptr;
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
