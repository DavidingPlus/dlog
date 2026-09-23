#include "logcolor.h"
#include "logger.h"

#include <array>
#include <cstdio>
#include <string_view>


namespace
{

    struct LogLevelExample
    {
        LogLevel level;
        std::string_view name;
    };

    void showLoggerColor(LogLevel level, std::string_view name)
    {
        // Logger 析构时输出 LogStream 缓冲区；先创建颜色守卫，保证整行输出时颜色仍有效。
        LogColorGuard color(level);
        Logger logger(__FILE__, __LINE__, level);
        logger.stream() << name << " color sample";
    }

    void showFatalColor()
    {
        // 直接输出 LogStream 缓冲区来预览 FATAL 颜色，避免触发 Logger 的 abort 行为。
        LogStream stream;
        stream << "FATAL color sample";
        const LogStream::Buffer &buffer = stream.buffer();

        LogColorGuard color(LogLevel::FATAL);
        std::fwrite(buffer.data(), sizeof(char), buffer.length(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

} // namespace


int main()
{
    constexpr std::array<LogLevelExample, 5> examples{{
        {LogLevel::TRACE, "TRACE"},
        {LogLevel::DEBUG, "DEBUG"},
        {LogLevel::INFO, "INFO"},
        {LogLevel::WARN, "WARN"},
        {LogLevel::ERROR, "ERROR"},
    }};

    for (const LogLevelExample &example : examples) showLoggerColor(example.level, example.name);

    showFatalColor();
}
