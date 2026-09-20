#ifndef _DLOG_LOGSTREAM_H_
#define _DLOG_LOGSTREAM_H_

#include "globalmacros.h"

#include "fixedbuffer.h"

#include <charconv>
#include <limits>
#include <system_error>
#include <type_traits>


// LogStream 类用于管理日志输出流，重载输出流运算符 <<，将各种类型的值写入内部缓冲区。
class LogStream
{

    CLASS_NONCOPYABLE(LogStream)

    // 定义一个 Buffer 类型，使用固定大小的缓冲区。
    using Buffer = FixedBuffer<kSmallBufferSize>;

public:

    LogStream() = default;

    // 将指定长度的字符数据追加到缓冲区。
    void append(const char *buffer, int len) { m_buffer.append(buffer, len); }

    // 返回当前缓冲区的常量引用。
    const Buffer &buffer() const { return m_buffer; }

    // 重置缓冲区，将当前指针重置到缓冲区的起始位置。
    void resetBuffer() { m_buffer.reset(); }

    LogStream &operator<<(bool express);

    LogStream &operator<<(short number);

    LogStream &operator<<(unsigned short number);

    LogStream &operator<<(int number);

    LogStream &operator<<(unsigned int number);

    LogStream &operator<<(long number);

    LogStream &operator<<(unsigned long number);

    LogStream &operator<<(long long number);

    LogStream &operator<<(unsigned long long number);

    LogStream &operator<<(float number);

    LogStream &operator<<(double number);

    LogStream &operator<<(char str);

    LogStream &operator<<(const char *str);

    LogStream &operator<<(const unsigned char *str);

    LogStream &operator<<(const std::string &str);

    // LogStream &operator<<(const GeneralTemplate &g);


private:

    // 模板函数。用于特殊格式化整型。
    template <typename T>
    void formatInteger(T num);


    // 内部缓冲区对象。
    Buffer m_buffer;
};


template <typename T>
void LogStream::formatInteger(T num)
{
    // 该函数只负责格式化整数。bool 虽然属于整型类别，但日志中有独立的 operator<<(bool) 实现，因此这里明确排除 bool，避免输出为 0/1。
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "formatInteger requires a non-bool integral type");

    // numeric_limits<T>::digits10 表示该类型可以可靠表示的十进制有效数字位数。
    // 在此基础上额外预留：1 个字符：整数可能需要的负号；1 个字符：最大值可能比 digits10 多出 1 位；1 个字符：额外的安全余量。
    // std::to_chars 不要求缓冲区末尾存在 '\0'，所以这里不需要专门为 C 字符串结束符预留空间。对于 int、long、long long 等标准整数类型，这个大小能够覆盖其完整的十进制表示。
    constexpr size_t bufferSize = std::numeric_limits<T>::digits10 + 3;
    char buffer[bufferSize];

    // std::to_chars 是 C++17 提供的数值转字符函数，定义在 <charconv> 中。它直接把数字写入 [first, last) 字符缓冲区，不创建临时 std::string，也不依赖当前区域设置，适合日志系统中的轻量级格式化。
    // std::to_chars 的三个参数版本用于整数格式化：
    //   1. buffer：输出缓冲区的起始位置；
    //   2. buffer + sizeof(buffer)：输出缓冲区的结束位置（结束位置不写入）；
    //   3. num：需要转换的整数，默认按十进制格式化。
    //
    // 返回值是 std::to_chars_result：
    //   - ptr：转换结果的结束位置；
    //   - ec：错误码，std::errc{} 表示转换成功。
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), num);

    // FixedBuffer 保存的是“起始地址 + 有效长度”，不要求末尾存在 '\0'。因此使用 ptr - buffer 得到实际字符数，避免调用 strlen 扫描缓冲区。
    if (std::errc{} == result.ec) m_buffer.append(buffer, static_cast<size_t>(result.ptr - buffer));
}


#endif
