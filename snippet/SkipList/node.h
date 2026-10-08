#ifndef _SKIPLIST_NODE_H_
#define _SKIPLIST_NODE_H_

#include <vector>


template <typename K, typename T>
class Node
{

public:

    Node() = default;

    Node(K key, T value, int level);

    // vector 会自动释放指针数组；指针指向的节点由 SkipList 管理。
    ~Node() = default;

    K get_key() const;

    T get_value() const;

    void set_value(T value);


    // forward[i] 保存第 i 层中下一个节点的地址；下标 0 是最底层。使用 vector 管理这些指针槽位。
    std::vector<Node<K, T> *> m_forward;

    // 当前节点所在的最高层；本节点参与第 0 层到 nodeLevel 层。
    int m_nodeLevel;


private:

    K m_key;

    T m_value;
};


#endif
