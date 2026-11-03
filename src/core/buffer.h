#ifndef _DLOG_BUFFER_H_
#define _DLOG_BUFFER_H_

#include "globalmacros.h"

#include <vector>
#include <string>


DLOG_NAMESPACE_BEGIN(dlog)


/**
 * @class Buffer
 * @brief Buffer 类其实是封装了一个用户缓冲区，以及向这个缓冲区写数据读数据等一系列控制方法。
 * @ref https://github.com/DavidingPlus/dmuduo/blob/master/src/core/buffer.h
 *
 * Buffer 使用一块连续内存，通过 m_readerIndex 和 m_writerIndex 两个索引分别表示读位置和写位置。
 *
 * 内存布局：
 *
 * | consumed | readable | writable |
 *            ^          ^
 *            |          |
 *      readerIndex  writerIndex
 *
 * 初始读写位置均为 0；consumed 区域表示已经接收，已经消费完的数据；readable 区域表示已经接收，待消费的数据；writable 区域表示可以写入数据的空间（没有数据）。
 *
 * 读取数据时不会移动内存，只移动 m_readerIndex，避免频繁 memcpy。
 *
 * 当可写空间不足时：
 * 1. 优先利用前面已经读取的数据空间；
 * 2. 如果空间仍然不足，则扩容。
 */
class DLOG_API_EXPORTED Buffer
{

public:

    // 初始预留的 readable 和 writable 数据空间大小。初始时 readableBytes 为 0，writableBytes 为 kInitialSize，这个是符合语义的。
    static constexpr size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize) : m_buffer(initialSize) {}

    ~Buffer() = default;

    size_t readableBytes() const { return m_writerIndex - m_readerIndex; }

    size_t writableBytes() const { return m_buffer.size() - m_writerIndex; }

    // 返回缓冲区中可读 readable 区域的起始地址。
    const char *peek() const { return m_buffer.empty() ? begin() : begin() + m_readerIndex; }

    // 注意：以下这四个 retrieve 系列函数操控的都是可读 readable 区的数据，并且都只是逻辑上消费数据，实际并不会修改 Buffer 内存，在实际操作中为了效率也不能频繁修改 Buffer 内存。它们只是在逻辑上移动指针，维持可读和可写的语义，表示这部分数据已经被上层处理。下面的注释用“消费”这个词语描述。

    // 消费指定长度的可读数据；len 达到或超过可读长度时消费全部并重置缓冲区。
    void retrieve(size_t len);

    // 消费当前 Buffer 中全部可读数据。消费完以后，Buffer 的状态应该回到初始状态。
    void retrieveAll() { reset(); }

    // 最多取 len 字节，复制为 string 并消费；len 达到或超过可读长度时取全部并重置缓冲区。
    std::string retrieveAsString(size_t len);

    // 将 Buffer 当前所有可读数据复制为 string 返回，并消费全部数据。
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes()); }

    // 保证 Buffer 至少拥有 len 字节的连续可写空间，不够就调用 makeSpace() 扩充。
    void ensureWritableBytes(size_t len);

    // 把 [data, data + len) 的数据追加到 writable 区。len 为 0 时不操作，正长度空指针会抛异常。
    // data 必须来自外部有效内存，不支持自追加，否则可能导致源指针失效，出现不可预期的问题。
    void append(const char *data, size_t len);

    // 返回缓冲区中可写 writable 区域的起始地址。
    char *beginWrite() { return m_buffer.empty() ? begin() : begin() + m_writerIndex; }

    const char *beginWrite() const { return m_buffer.empty() ? begin() : begin() + m_writerIndex; }

    void reset() { m_readerIndex = 0, m_writerIndex = 0; }


private:

    // 返回缓冲区首个字节的地址。
    char *begin() { return m_buffer.data(); }

    const char *begin() const { return m_buffer.data(); }

    void makeSpace(size_t len);


    std::vector<char> m_buffer;

    size_t m_readerIndex = 0;

    size_t m_writerIndex = 0;
};


DLOG_NAMESPACE_END


#endif
