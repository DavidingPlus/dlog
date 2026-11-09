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

    // 整块存储的起点及总字节数；capacity 包含已消费、可读和尾部可写区域。
    char *storageData() { return m_buffer.data(); }

    const char *storageData() const { return m_buffer.data(); }

    size_t capacity() const { return m_buffer.size(); }

    size_t readableBytes() const { return m_writerIndex - m_readerIndex; }

    size_t writableBytes() const { return m_buffer.size() - m_writerIndex; }

    // 返回缓冲区中可读 readable 区域的起始地址。
    const char *readableBegin() const { return m_buffer.empty() ? storageData() : storageData() + m_readerIndex; }

    // 复制可读数据，不消费；显式长度保留内嵌空字节。
    std::string toString() const { return readableBytes() ? std::string(readableBegin(), readableBytes()) : std::string(); }

    // consume 系列只推进读索引，不移动或清零数据。

    // 消费指定长度的可读数据；len 达到或超过可读长度时消费全部并重置缓冲区。
    void consume(size_t len);

    // 最多取 len 字节，复制为 string 并消费；len 达到或超过可读长度时取全部并重置缓冲区。
    std::string consumeAsString(size_t len);

    // 将 Buffer 当前所有可读数据复制为 string 返回，并消费全部数据。
    std::string consumeAllAsString() { return consumeAsString(readableBytes()); }

    // 保证 Buffer 至少拥有 len 字节的连续可写空间，不够就调用 makeSpace() 扩充。
    void ensureWritableBytes(size_t len);

    // 把 [data, data + len) 的数据追加到 writable 区。len 为 0 时不操作，正长度空指针会抛异常。
    // data 必须来自外部有效内存，不支持自追加，否则可能导致源指针失效，出现不可预期的问题。
    void append(const char *data, size_t len);

    // 返回缓冲区中可写 writable 区域的起始地址。
    char *writableBegin() { return m_buffer.empty() ? storageData() : storageData() + m_writerIndex; }

    const char *writableBegin() const { return m_buffer.empty() ? storageData() : storageData() + m_writerIndex; }

    // 清空逻辑数据，保留存储空间及原字节。
    void clear() { m_readerIndex = 0, m_writerIndex = 0; }


private:

    void makeSpace(size_t len);


    std::vector<char> m_buffer;

    size_t m_readerIndex = 0;

    size_t m_writerIndex = 0;
};


DLOG_NAMESPACE_END


#endif
