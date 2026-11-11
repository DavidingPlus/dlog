#include "logcolorguard.h"
#include "log.h"

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
        auto logger = dlog::LogManager::GetInstance().getDefaultLogger();
        // dlog::LogColorGuard color(logger.stream(), example.level);
        logger->message(__FILE__, __LINE__, example.level).stream() << example.name << " (" << example.colorName << ") color sample";
    }

    void showFatalColor()
    {
        DLOG_FATAL() << "FATAL (spdlog CRITICAL, bold on red) color sample";
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
