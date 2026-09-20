#ifndef _DLOG_FIXEDBUFFER_H_
#define _DLOG_FIXEDBUFFER_H_

#include "globalmacros.h"


// 使用模板参数指定缓冲区容量，使缓冲区大小在编译期确定，并将字符数组直接嵌入 FixedBuffer 对象中，避免缓冲区内部额外的动态内存分配（类似 std::array<>）。
template <int bufferSize>
class FixedBuffer
{

    CLASS_NONCOPYABLE(FixedBuffer)

public:


private:

    // 定义固定大小的缓冲区。
    char m_data[bufferSize] = {0};

    // 当前指针，指向缓冲区中下一个可写入的位置。
    char *m_cur = nullptr;

    // 缓冲区的大小。
    int m_size = 0;
};


#endif
