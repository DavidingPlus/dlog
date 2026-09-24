#include "logcolorguard.h"
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
        std::string_view colorName;
    };

    void showLoggerColor(const LogLevelExample &example)
    {
        Logger logger(__FILE__, __LINE__, example.level);
        // LogColorGuard color(logger.stream(), example.level);
        logger.stream() << example.name << " (" << example.colorName << ") color sample";
    }

    void showFatalColor()
    {
        LogStream stream;
        {
            // LogColorGuard color(stream, LogLevel::FATAL);
            stream << "FATAL (spdlog CRITICAL, bold on red) color sample";
        }

        const SmallBuffer &buffer = stream.buffer();
        std::fwrite(buffer.data(), sizeof(char), buffer.length(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

} // namespace


int main()
{
    constexpr std::array<LogLevelExample, 5> examples{{
        {LogLevel::TRACE, "TRACE", "white"},
        {LogLevel::DEBUG, "DEBUG", "cyan"},
        {LogLevel::INFO, "INFO", "green"},
        {LogLevel::WARN, "WARN", "bold yellow"},
        {LogLevel::ERROR, "ERROR", "bold red"},
    }};

    for (const LogLevelExample &example : examples) showLoggerColor(example);

    showFatalColor();
}
