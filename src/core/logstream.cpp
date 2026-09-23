#include "logstream.h"


LogStream &LogStream::operator<<(bool express)
{
    m_buffer.append(express ? "true" : "false", express ? 4 : 5);
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

LogStream &LogStream::operator<<(const char *str)
{
    // const char* 表示以 '\0' 结尾的 C 字符串。转换为 string_view 时仍然由构造函数查找字符串长度，但最终追加统一走 string_view 重载。
    if (!str) return *this;

    return *this << std::string_view(str);
}

LogStream &LogStream::operator<<(const unsigned char *str)
{
    // unsigned char* 不能隐式转换为 string_view，需要显式转换为 const char*。
    // 这里仍按 '\0' 结尾的 C 字符串处理；二进制数据应使用带长度的 std::string_view(data, length) 调用，避免被嵌入式 '\0' 截断。
    if (!str) return *this;

    return *this << std::string_view(reinterpret_cast<const char *>(str));
}

LogStream &LogStream::operator<<(std::string_view sv)
{
    // string_view::size() 是数据的显式长度，不会像 strlen() 一样在遇到 '\0' 时提前停止。
    // FixedBuffer 同样按照“地址 + 长度”保存数据，因此可以完整写入包含嵌入式 '\0' 的字符序列。
    if (!sv.empty()) m_buffer.append(sv.data(), sv.size());
    return *this;
}
