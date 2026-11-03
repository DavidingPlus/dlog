#include "buffer.h"

#include <algorithm>


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
    // 消费完全部可读数据以后，Buffer 的状态应该回到初始状态。
    m_readerIndex = 0;
    m_writerIndex = 0;
}

std::string Buffer::retrieveAsString(size_t len)
{
    if (0 == len) return std::string();

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
    if (0 == len) return;

    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    m_writerIndex += len;
}

void Buffer::makeSpace(size_t len)
{
    // 初始情况下，全部 initialSize 字节都是可写空间。
    // 在 Buffer 工作中，需明确两个方向的语义：
    // append() 向 writable 区域写入数据，使其成为 readable 数据。
    // retrieve() 消费 readable 数据，留下可回收的空间，下面用 xxx 表示。

    // 某时刻内存布局长这样：
    // | xxx | readable | writable |，xxx 表示已经消费的数据。
    // 整理后：| readable | writable |。

    // xxx 是已读的数据，是可以被回收的垃圾数据，因此现在实际上可写的空闲区域是 xxx 加 writable，如果这二者加起来大于等于 len，根本没必要扩容，只需要移动 readable 区的数据即可。
    if (writableBytes() + m_readerIndex >= len)
    {
        // 存储原可读区域的大小，防止后续因为修改指针导致 readableBytes() 失效而错误。
        size_t readable = readableBytes();

        // 将未消费的数据移到起点，复用前部空间。
        std::copy(begin() + m_readerIndex, begin() + m_writerIndex, begin());

        m_readerIndex = 0;
        m_writerIndex = readable;
    }
    // 否则需要扩容，这里选择暂时不处理 xxx 已读部分的数据，扩容只扩充数组尾部，因此前面的指针都不用动。
    else
    {
        m_buffer.resize(m_writerIndex + len);
    }
}


DLOG_NAMESPACE_END
