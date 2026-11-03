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
 * | prependable | readable | writable |
 *              ^          ^
 *              |          |
 *        readerIndex  writerIndex
 *
 * readable 区域表示已经接收的数据；writable 区域表示可以写入的数据。
 *
 * 读取数据时不会移动内存，只移动 m_readerIndex，避免频繁 memcpy，提高网络 IO 性能。
 *
 * 当可写空间不足时：
 * 1. 优先利用前面已经读取的数据空间；
 * 2. 如果空间仍然不足，则扩容。
 *
 * 前面预留 prependable 空间，用于存放协议头，例如长度字段，避免插入数据时移动已有内容。
 */
class DLOG_API_EXPORTED Buffer
{

public:

    // 初始预留的 prependable 空间大小。
    static constexpr size_t kPrependSize = 8;

    // 初始预留的 readable 和 writable 数据空间大小。初始时 readableBytes 为 0，writableBytes 为 kInitialSize，这个是符合语义的。
    static constexpr size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize) : m_buffer(kPrependSize + initialSize) {}

    ~Buffer() = default;

    size_t readableBytes() const { return m_writerIndex - m_readerIndex; }

    size_t writableBytes() const { return m_buffer.size() - m_writerIndex; }

    // 返回缓冲区中可读 readable 区域的起始地址。
    const char *peek() const { return begin() + m_readerIndex; }

    // 注意：以下这四个 retrieve 系列函数操控的都是可读 readable 区的数据，并且都只是逻辑上消费数据，实际并不会修改 Buffer 内存，在实际操作中为了效率也不能频繁修改 Buffer 内存。它们只是在逻辑上移动指针，维持可读和可写的语义，表示这部分数据已经被上层处理。下面的注释用“消费”这个词语描述。

    // 消费指定长度的可读数据。
    void retrieve(size_t len);

    // 消费当前 Buffer 中全部可读数据。
    void retrieveAll();

    // 将指定长度的可读数据复制为 string 返回，并消费这些数据。
    std::string retrieveAsString(size_t len);

    // 将 Buffer 当前所有可读数据复制为 string 返回，并消费全部数据。
    std::string retrieveAllAsString() { return retrieveAsString(readableBytes()); }

    // 保证 Buffer 至少拥有 len 字节的连续可写空间，不够就调用 makeSpace() 扩充。
    void ensureWritableBytes(size_t len);

    // 把 [data, data + len] 内存上的数据添加到 writable 缓冲区当中。
    void append(const char *data, size_t len);

    // 返回缓冲区中可写 writable 区域的起始地址。
    char *beginWrite() { return begin() + m_writerIndex; }

    const char *beginWrite() const { return begin() + m_writerIndex; }

    // fd -> Buffer，把新数据写入 writable 区域，然后让它加入 readable 区域。如果 Buffer 当前可写空间不足，会先借助栈上的临时缓冲区（extraBuf）接收剩余数据，再追加到 Buffer。因此 readFd() 不仅负责执行 readv()，还负责维护 Buffer 的内部状态（writerIndex），使新读入的数据立即成为 readable 区域。
    // errno 通过引用返回，语义上表示“调用者必须提供一个可写的错误码接收对象”。
    ssize_t readFd(int fd, int &saveErrno);

    // Buffer -> fd，把 readable 区域的数据发送出去。
    // 注意：writeFd() 只负责调用 write() 发送数据，并返回实际发送的字节数，不会修改 Buffer，也不会消费 readable 数据。是否调用 retrieve() 移除已发送的数据，由上层（如 TcpConnection::handleWrite()）根据发送结果决定，这样可以在发送完成后继续执行关闭写事件、触发 writeComplete 回调、半关闭连接等后续逻辑，职责划分更加清晰。
    ssize_t writeFd(int fd, int &saveErrno);


private:

    // 返回缓冲区首个字节的地址。
    char *begin() { return &*m_buffer.begin(); }

    const char *begin() const { return &*m_buffer.begin(); }

    void makeSpace(size_t len);


    std::vector<char> m_buffer;

    size_t m_readerIndex = kPrependSize;

    size_t m_writerIndex = kPrependSize;
};


DLOG_NAMESPACE_END


#endif
