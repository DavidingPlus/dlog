#include "logstream.h"

#include <charconv>
#include <cstring>
#include <system_error>


LogStream &LogStream::operator<<(bool express)
{
    m_buffer.append(express ? "true" : "false", express ? 4 : 5);
    return *this;
}

LogStream &LogStream::operator<<(short number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(unsigned short number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(int number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(unsigned int number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(long number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(unsigned long number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(long long number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(unsigned long long number)
{
    formatInteger(number);
    return *this;
}

LogStream &LogStream::operator<<(float number)
{
    *this << static_cast<double>(number);
    return *this;
}

LogStream &LogStream::operator<<(double number)
{
    // max_digits10 表示保证浮点值往返转换所需的有效十进制数字位数，它不是最终字符串的总长度。因此额外预留空间，用于负号、小数点、科学计数法中的 e、指数符号和指数数字。
    constexpr size_t bufferSize = std::numeric_limits<double>::max_digits10 + 8;
    char buffer[bufferSize];

    // std::chars_format::general 会根据数值大小自动选择普通表示法或科学计数法。
    // precision == 12 对应 "%.12g" 的格式意图，即保留约 12 位有效数字。
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), number, std::chars_format::general, 12);

    if (std::errc{} == result.ec) m_buffer.append(buffer, static_cast<size_t>(result.ptr - buffer));


    return *this;
}

LogStream &LogStream::operator<<(char str)
{
    m_buffer.append(&str, 1);
    return *this;
}

LogStream &LogStream::operator<<(const char *str)
{
    m_buffer.append(str, std::strlen(str));
    return *this;
}

LogStream &LogStream::operator<<(const unsigned char *str)
{
    m_buffer.append(reinterpret_cast<const char *>(str), std::strlen(reinterpret_cast<const char *>(str)));
    return *this;
}

LogStream &LogStream::operator<<(const std::string &str)
{
    m_buffer.append(str.c_str(), str.size());
    return *this;
}
