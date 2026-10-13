#ifndef _SKIPLIST_NODE_H_
#define _SKIPLIST_NODE_H_

#include <vector>


template <typename K, typename T>
class Node
{

public:

    Node() = default;

    // level 是该节点所在的最高层编号，故创建 level + 1 个槽位，编号为 [0, level]。
    Node(K key, T value, int level) : m_forward(1 + level, nullptr), m_nodeLevel(level), m_key(key), m_value(value) {}

    ~Node() = default;

    K getKey() const { return m_key; }

    T getValue() const { return m_value; }

    void setValue(T value) { m_value = value; }


    // 一个 Node 对象只有一份；下图中同一个 key 跨层出现，是它参与多层的示意，不是多份节点。m_forward[i] 指向第 i 层中的下一个横向后继节点。
    //
    // 插入 50 后的层级示意：
    // Level 4: HEAD -> 1 ----------------------------------------------> 100
    // Level 3: HEAD -> 1 -> 10 -----------------> 50 -> 70 -----------> 100
    // Level 2: HEAD -> 1 -> 10 -> 30 -> 50 -> 70 ---------------------> 100
    // Level 1: HEAD -> 1 -> 4 -> 10 -> 30 -> 50 -> 70 ---------------> 100
    // Level 0: HEAD -> 1 -> 4 -> 9 -> 10 -> 30 -> 40 -> 50 -> 60 -> 70 -> 100
    //
    // Node 50 参与第 0 到 3 层：m_nodeLevel=3，m_forward.size()=4。m_forward[0] 指向 60，m_forward[1]、[2]、[3] 都指向 70。
    // Node 100 参与第 0 到 4 层：m_nodeLevel=4，m_forward.size()=5；它是最右节点，所有槽位为 nullptr。
    std::vector<Node<K, T> *> m_forward;

    // 当前节点所在的最高层编号；本节点参与第 0 层到 m_nodeLevel 层（含两端）。
    int m_nodeLevel;


private:

    K m_key;

    T m_value;
};


#endif
