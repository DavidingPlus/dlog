#ifndef _DLOG_LOGSTREAM_H_
#define _DLOG_LOGSTREAM_H_

#include "globalmacros.h"

#include "fixedbuffer.h"


// LogStream 类用于管理日志输出流，重载输出流运算符 <<，将各种类型的值写入内部缓冲区。
class LogStream
{

    CLASS_NONCOPYABLE(LogStream)

    // 定义一个 Buffer 类型，使用固定大小的缓冲区。
    using Buffer = FixedBuffer<kSmallBufferSize>;

public:

    // 将指定长度的字符数据追加到缓冲区。
    void append(const char *buffer, int len) { m_buffer.append(buffer, len); }

    // 返回当前缓冲区的常量引用。
    const Buffer &buffer() const { return m_buffer; }

    // 重置缓冲区，将当前指针重置到缓冲区的起始位置。
    void resetBuffer() { m_buffer.reset(); }


private:

    // 内部缓冲区对象。
    Buffer m_buffer;
};


#endif
