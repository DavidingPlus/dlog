#ifndef _SKIPLIST_SKIPLIST_H_
#define _SKIPLIST_SKIPLIST_H_

#include "node.h"


template <typename K, typename T>
class SkipList
{

public:

    explicit SkipList(int maxLevel);

    ~SkipList();

    // 插入键值对：返回 0 表示成功，返回 1 表示 key 已存在。
    int insertElement(const K &key, const T &value);

    bool searchElement(const K &key) const;

    void deleteElement(const K &key);

    void displayList() const;

    int size() const;


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
SkipList<K, T>::SkipList(int maxLevel)
    : m_maxLevel(maxLevel)
{
}

template <typename K, typename T>
SkipList<K, T>::~SkipList()
{
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
}

template <typename K, typename T>
int SkipList<K, T>::size() const
{
    return m_elementCount;
}

template <typename K, typename T>
int SkipList<K, T>::getRandomLevel()
{
    return 0;
}

template <typename K, typename T>
Node<K, T> *SkipList<K, T>::createNode(const K &, const T &, int)
{
    return nullptr;
}

template <typename K, typename T>
void SkipList<K, T>::clear(Node<K, T> *)
{
}


#endif
