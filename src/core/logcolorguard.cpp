#include "logcolorguard.h"

#include "logger.h"

#include <array>


namespace
{

    constexpr std::array<std::string_view, static_cast<std::size_t>(LogLevel::LEVEL_COUNT)> kLogLevelColors{
        DLOG_COLOR_TRACE,
        DLOG_COLOR_DEBUG,
        DLOG_COLOR_INFO,
        DLOG_COLOR_WARN,
        DLOG_COLOR_ERROR,
        DLOG_COLOR_FATAL,
    };

    std::string_view logLevelColor(LogLevel level) noexcept { return kLogLevelColors[static_cast<size_t>(level)]; }

}


LogColorGuard::LogColorGuard(LogStream &stream, LogLevel level)
    : m_stream(stream)
{
    m_stream << logLevelColor(level);
}

LogColorGuard::~LogColorGuard()
{
    m_stream << DLOG_COLOR_RESET;
}
