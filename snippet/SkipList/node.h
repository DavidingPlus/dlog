#ifndef _SKIPLIST_NODE_H_
#define _SKIPLIST_NODE_H_

#include <vector>


template <typename K, typename T>
class Node
{

public:

    Node() = default;

    Node(K key, T value, int level) : m_forward(1 + level, nullptr), m_nodeLevel(level), m_key(key), m_value(value) {}

    ~Node() = default;

    K getKey() const { return m_key; }

    T getValue() const { return m_value; }

    void setValue(T value) { m_value = value; }


    // forward[i] 保存第 i 层中下一个节点的地址；下标 0 是最底层。使用 vector 管理这些指针槽位。
    std::vector<Node<K, T> *> m_forward;

    // 当前节点所在的最高层；本节点参与第 0 层到 nodeLevel 层。
    int m_nodeLevel;


private:

    K m_key;

    T m_value;
};


#endif
