#ifndef _DLOG_FIXEDBUFFER_H_
#define _DLOG_FIXEDBUFFER_H_

#include "globalmacros.h"


// 使用模板参数指定缓冲区容量，使缓冲区大小在编译期确定，并将字符数组直接嵌入 FixedBuffer 对象中，避免缓冲区内部额外的动态内存分配（类似 std::array<>）。
template <int bufferSize>
class FixedBuffer
{

    CLASS_NONCOPYABLE(FixedBuffer)

public:

    // 构造函数，初始化当前指针为缓冲区的起始位置。
    FixedBuffer() : m_cur(m_data) {}

    // 返回缓冲区的起始地址。
    const char *data() const { return m_data; }

    // 返回当前指针的位置。
    char *current() { return m_cur; }

    // 返回缓冲区中当前有效数据的长度。
    int length() const { return m_size; }

    // 返回缓冲区中剩余可用空间的大小。
    size_t avail() const { return static_cast<size_t>(bufferSize - m_size); }

    // 将缓冲区中的数据转换为 std::string 类型并返回。
    std::string toString() const { return std::string(m_data, length()); }

    // 清空缓冲区的数据。
    void bzero() { ::bzero(m_data, bufferSize); }

    // 将指定长度的数据追加到缓冲区。如果缓冲区有足够的可用空间，则将数据复制到当前指针位置，并更新当前指针。
    void append(const char *buf, size_t len);

    // 更新当前指针，增加指定长度。
    void add(size_t len);

    // 重置当前指针，回到缓冲区的起始位置。
    void reset();


private:

    // 定义固定大小的缓冲区。
    char m_data[bufferSize] = {0};

    // 当前指针，指向缓冲区中下一个可写入的位置。
    char *m_cur = nullptr;

    // 缓冲区的大小。
    int m_size = 0;
};


template <int bufferSize>
void FixedBuffer<bufferSize>::append(const char *buf, size_t len)
{
}

template <int bufferSize>
void FixedBuffer<bufferSize>::add(size_t len)
{
}

template <int bufferSize>
void FixedBuffer<bufferSize>::reset()
{
}


#endif
