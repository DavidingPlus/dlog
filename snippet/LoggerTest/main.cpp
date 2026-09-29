#include "logcolorguard.h"
#include "logger.h"

#include <cerrno>
#include <array>
#include <cstdio>
#include <string_view>


namespace
{

    struct LogLevelExample
    {
        dlog::LogLevel level;
        std::string_view name;
        std::string_view colorName;
    };

    void showLoggerColor(const LogLevelExample &example)
    {
        dlog::Logger logger(__FILE__, __LINE__, example.level);
        // dlog::LogColorGuard color(logger.stream(), example.level);
        logger.stream() << example.name << " (" << example.colorName << ") color sample";
    }

    void showFatalColor()
    {
        dlog::LogStream stream;
        {
            // dlog::LogColorGuard color(stream, dlog::LogLevel::FATAL);
            stream << "FATAL (spdlog CRITICAL, bold on red) color sample";
        }

        const dlog::SmallBuffer &buffer = stream.buffer();
        std::fwrite(buffer.data(), sizeof(char), buffer.length(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

} // namespace


int main()
{
    constexpr std::array<LogLevelExample, 5> examples{{
        {dlog::LogLevel::TRACE, "TRACE", "white"},
        {dlog::LogLevel::DEBUG, "DEBUG", "cyan"},
        {dlog::LogLevel::INFO, "INFO", "green"},
        {dlog::LogLevel::WARN, "WARN", "bold yellow"},
        {dlog::LogLevel::ERROR, "ERROR", "bold red"},
    }};

    for (const LogLevelExample &example : examples) showLoggerColor(example);
    DLOG_SYS_ERROR(ENOENT);

    showFatalColor();
}
