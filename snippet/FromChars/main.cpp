#include <charconv>
#include <iostream>
#include <limits>
#include <string_view>
#include <system_error>


bool tryParseNonNegativeInt(std::string_view text, int &value)
{
    const char *first = text.data();
    const char *last = first + text.size();

    // 使用 unsigned int 解析，from_chars() 会直接拒绝负号。
    unsigned int parsedValue = 0;
    auto res = std::from_chars(first, last, parsedValue, 10);

    // 三个条件分别检查：解析错误、没有完整消费文本、超出 int 可表示范围。
    if (std::errc{} != res.ec || last != res.ptr || parsedValue > static_cast<unsigned int>(std::numeric_limits<int>::max()))
    {
        return false;
    }
    else
    {
        value = static_cast<int>(parsedValue);
        return true;
    }
}

void check(std::string_view text)
{
    int value = 0;
    bool success = tryParseNonNegativeInt(text, value);

    std::cout << '"' << text << "\" -> " << (success ? "accepted" : "rejected");
    if (success) std::cout << ", value = " << value;
    std::cout << '\n';
}


int main()
{
    check("0");
    check("0012");
    check("-1");
    check("+1");
    check("12x");
    check("2147483647");
    check("2147483648");
    check("");
}
