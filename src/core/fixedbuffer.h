#ifndef _DLOG_FIXEDBUFFER_H_
#define _DLOG_FIXEDBUFFER_H_

#include "globalmacros.h"

#include <string>
#include <cstring>


// 日志系统预设的小缓冲区和大缓冲区容量。
// 这两个值是编译期常量，调用方包含头文件后即可直接参与数组长度和模板参数计算，例如 FixedBuffer<kSmallBufferSize> 等价于 FixedBuffer<4000>，不需要在运行时从 DLL 中读取一个变量。因此这里不使用 D_API_EXPORTED 导出变量符号。
// inline 允许该定义出现在多个源文件包含的头文件中，并按照 C++17 的 inline 变量规则处理重复定义；constexpr 则保证它可以作为常量表达式使用。修改容量会影响模板类型和对象布局，发布新版本时需要让使用方重新编译。
inline constexpr size_t kSmallBufferSize = 4000;
inline constexpr size_t kLargeBufferSize = 1000 * kSmallBufferSize;


// 缓冲区有两种常见设计：
// 1. C 字符串缓冲区：必须预留一个字节存放 '\0'。例如 bufferSize 为 8 时，最多存放 7 个有效字符，并保证 data() 返回的内容可以直接作为 C 字符串使用。
// 2. 带显式长度的字节缓冲区：不要求末尾存在 '\0'，有效数据范围由 data() 和 length() 共同确定。例如 bufferSize 为 8 时，可以存放 8 个有效字节，也可以正确保存中间包含 '\0' 的数据。
// FixedBuffer 采用第二种设计。bufferSize 表示全部可用的有效数据容量，不包含额外的 '\0' 保留位。因此 data() 只返回数据起始地址，不保证返回 C 字符串；读取数据时必须同时使用 length()，或调用 toString()。使用 data() 时不要调用依赖 '\0' 结尾的接口（例如 printf("%s", data())）。append() 在剩余空间不足时不会写入数据，也不会通过返回值报告失败，调用方应先通过 avail() 确认空间；reset() 只清除逻辑写入状态，bzero() 只清零物理内存且不改变状态。
// 使用模板参数指定缓冲区容量，使缓冲区大小在编译期确定，并将字符数组直接嵌入 FixedBuffer 对象中，避免缓冲区内部额外的动态内存分配（类似 std::array<>）。
template <size_t bufferSize>
class FixedBuffer
{

    D_CLASS_NONCOPYABLE(FixedBuffer)

public:

    // 构造函数，初始化当前指针为缓冲区的起始位置。
    FixedBuffer() : m_cur(m_data) {}

    // 返回缓冲区的起始地址。
    const char *data() const { return m_data; }

    // 返回当前指针的位置。不推荐调用方绕过 append() 直接修改这块内存。
    char *current() { return m_cur; }

    // 返回缓冲区中当前有效数据的长度。
    size_t length() const { return m_size; }

    // 返回缓冲区中剩余可用空间的大小。
    size_t avail() const { return bufferSize - m_size; }

    // 按当前有效长度将缓冲区中的数据转换为 std::string 类型并返回。
    std::string toString() const { return std::string(m_data, length()); }

    // 将缓冲区的物理内存清零；不改变当前有效长度和写入位置。
    void bzero() { std::memset(m_data, 0, bufferSize); }

    // 将指定长度的数据复制到当前写入位置，并更新缓冲区的写入状态。
    void append(const char *buf, size_t len);

    // 清除当前写入状态，使缓冲区恢复为空并从起始位置重新写入。
    void reset();


private:

    // 在数据写入 m_cur 指向的空间后，更新缓冲区的写入状态。m_cur 向后移动 len 个字节，m_size 增加 len。
    // 本函数不负责写入数据。调用前必须保证 len 不超过剩余可写空间。
    void updateWriteState(size_t len);


    // 定义固定大小的缓冲区。
    char m_data[bufferSize] = {0};

    // 当前写入位置，指向缓冲区中下一个可写入的位置。
    char *m_cur = nullptr;

    // 当前有效数据的长度。
    size_t m_size = 0;
};


template <size_t bufferSize>
void FixedBuffer<bufferSize>::append(const char *buf, size_t len)
{
    if (len <= avail())
    {
        // 复制数据到缓冲区。
        std::memcpy(m_cur, buf, len);
        // 更新缓冲区状态。
        updateWriteState(len);
    }
}

template <size_t bufferSize>
void FixedBuffer<bufferSize>::reset()
{
    m_cur = m_data;
    m_size = 0;
}

template <size_t bufferSize>
void FixedBuffer<bufferSize>::updateWriteState(size_t len)
{
    m_cur += len;
    m_size += len;
}


// SmallBuffer 用于 LogStream 格式化单条日志，容量为 4,000 字节。
// LargeBuffer 用于 AsyncLogging 的生产者缓冲区和待写缓冲块，容量为 4,000,000 字节，即 SmallBuffer 的 1,000 倍。
// using 为对应的 FixedBuffer 特化提供语义名称的别名。它既不创建新的派生类型，也不负责生成模板代码。
using SmallBuffer = FixedBuffer<kSmallBufferSize>;
using LargeBuffer = FixedBuffer<kLargeBufferSize>;


// 模板显式实例化声明：这两个常用特化的非内联模板成员由 .cpp 提供，其他翻译单元无需重复隐式实例化。因为模板定义仍保留在本头文件中，因此调用方仍可使用 FixedBuffer<8> 等其他容量；未列出的特化照常按需实例化。
extern template class FixedBuffer<kSmallBufferSize>;
extern template class FixedBuffer<kLargeBufferSize>;


#endif
