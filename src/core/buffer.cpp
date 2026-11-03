#include "buffer.h"

#include <unistd.h>
#include <sys/uio.h>


DLOG_NAMESPACE_BEGIN(dlog)


void Buffer::retrieve(size_t len)
{
    // 说明应用只读取了可读缓冲区数据的一部分，就是 len 长度，还剩下 [m_readerIndex+=len, m_writerIndex] 的数据未读。
    if (len < readableBytes())
    {
        m_readerIndex += len;
    }
    // len == readableBytes()
    else
    {
        retrieveAll();
    }
}

void Buffer::retrieveAll()
{
    // 消费完全部可读数据以后，Buffer 的状态应该回到初始状态。这也是为什么不能直接调用 retrieve(readableBytes())。
    m_readerIndex = kPrependSize;
    m_writerIndex = kPrependSize;
}

std::string Buffer::retrieveAsString(size_t len)
{
    std::string res(peek(), len);
    retrieve(len);
    return res;
}

void Buffer::ensureWritableBytes(size_t len)
{
    if (writableBytes() < len) makeSpace(len);
}

void Buffer::append(const char *data, size_t len)
{
    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    m_writerIndex += len;
}

ssize_t Buffer::readFd(int fd, int &saveErrno)
{
    // 从 socket 读取数据到 Buffer，如果 Buffer 放不下，就先放到栈上的临时缓冲区，再追加到 Buffer。
    // 注意：这里无法使用正常的扩容语义，因为我们并不知道 TCP 流过来的数据有多大，因此使用栈上的临时缓冲区，仅用于接收 Buffer 剩余空间放不下的数据。
    // 如果本次收到的数据超过 Buffer + extraBuf 的总容量，readv() 函数只会读取能够容纳的部分，剩余数据仍保留在内核 Socket 接收缓冲区，等待下一次 readFd() 再继续，不会出现数据丢失的问题。从这个设计上讲，其实不用 extraBuf 也不用担心数据丢失，为了提高效率这样做是值得的。

    // 栈上的临时缓冲区。当 Buffer 剩余可写空间不足时，readv 会将超出部分的数据暂存到这里，后续再通过 append() 追加到 Buffer 中。
    // 选择 64 KB 是为了在内存占用和单次读取效率之间取得平衡：大部分网络数据可以在一次 readv() 中完成读取，减少系统调用次数；同时临时缓冲区位于栈上，不能设置过大，避免占用过多线程栈空间。如果数据超过 Buffer + extrabuf 的容量，剩余数据仍会保留在内核 Socket 接收缓冲区，等待下一次读取。
    char extraBuf[64 * 1024] = {0};

    /*
    struct iovec {
        ptr_t iov_base; // iov_base 指向的缓冲区存放的是 readv 所接收的数据或是 writev 将要发送的数据。
        size_t iov_len; // iov_len 在各种情况下分别确定了接收的最大长度以及实际写入的长度。
    };
    */

    // 使用 iovec 分配两个连续的缓冲区。
    struct iovec vec[2];
    // 存储当前 Buffer 空间剩余可写空间的大小，防止后续因为修改指针导致 writableBytes() 失效而错误。
    const size_t writable = writableBytes();

    // 第一块缓冲区，指向可写空间。
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    // 第二块缓冲区，指向栈空间。
    vec[1].iov_base = extraBuf;
    vec[1].iov_len = sizeof(extraBuf);

    // 如果第一个缓冲区 >= 64 KB，那我们上面担心 Buffer 不够是多余的，就只采用一个缓冲区。
    const int iovcnt = (writable < sizeof(extraBuf)) ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);

    if (n < 0)
    {
        saveErrno = errno;
    }
    // Buffer 的可写缓冲区已经够存储读出来的数据了。
    else if (n <= writable)
    {
        m_writerIndex += n;
    }
    // extraBuf 里面也写入了 n-writable 长度的数据。
    else
    {
        m_writerIndex = m_buffer.size();
        // 对 m_buffer 扩容，并将 extraBuf 存储的另一部分数据追加至 m_buffer。
        append(extraBuf, n - writable);
    }


    return n;
}

ssize_t Buffer::writeFd(int fd, int &saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0) saveErrno = errno;
    return n;
}

void Buffer::makeSpace(size_t len)
{
    // 初始情况下，内存布局是这样的 | prependable | kInitialSize |
    // 在 Buffer 工作中，需明确两个方向的语义：
    // readFd：fd -> Buffer，有新数据到来的时候，一定是把新数据写入 writable 区域，然后让它加入 readable 区域。
    // writeFd：Buffer -> fd，把 readable 区域的数据发送出去，然后消费 readable 数据，因此会出现 readable 已读的部分，下面用 xxx 表示。

    // 某时刻内存布局长这样：
    // | kPrependSize | xxx | readable | writable |，xxx 表示 readable 中已读的部分。
    // | kPrependSize | readable ｜ len |

    // xxx 是已读的数据，是可以被回收的垃圾数据，因此现在实际上可写的空闲区域是 xxx 加 writable，如果这二者加起来大于等于 len，根本没必要扩容，只需要移动 readable 区的数据即可。
    if (writableBytes() + m_readerIndex - kPrependSize >= len)
    {
        // 存储原可读区域的大小，防止后续因为修改指针导致 readableBytes() 失效而错误。
        size_t readable = readableBytes();

        // 将当前缓冲区中从 m_readerIndex 到 m_writerIndex 的数据，拷贝到缓冲区起始位置 kPrependSize 处，以便腾出更多的可写空间。
        std::copy(begin() + m_readerIndex, begin() + m_writerIndex, begin() + kPrependSize);

        m_readerIndex = kPrependSize;
        m_writerIndex = m_readerIndex + readable;
    }
    // 否则需要扩容，这里选择暂时不处理 xxx 已读部分的数据，扩容只扩充数组尾部，因此前面的指针都不用动。
    else
    {
        m_buffer.resize(m_writerIndex + len);
    }
}


DLOG_NAMESPACE_END
