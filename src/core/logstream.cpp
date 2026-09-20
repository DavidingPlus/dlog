#include "logstream.h"

#include <charconv>
#include <cstring>
#include <system_error>


// TODO
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
    char buffer[32];

    const auto result = std::to_chars(
        buffer,
        buffer + sizeof(buffer),
        number,
        std::chars_format::general,
        12);

    if (result.ec == std::errc{})
    {
        m_buffer.append(buffer, static_cast<size_t>(result.ptr - buffer));
    }

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
