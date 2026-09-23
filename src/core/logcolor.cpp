#include "logcolor.h"

#include "logger.h"


LogColorGuard::LogColorGuard(LogLevel level)
{
    switch (level)
    {
        case LogLevel::TRACE:
            std::cout << DLOG_COLOR_TRACE;
            break;
        case LogLevel::DEBUG:
            std::cout << DLOG_COLOR_DEBUG;
            break;
        case LogLevel::INFO:
            std::cout << DLOG_COLOR_INFO;
            break;
        case LogLevel::WARN:
            std::cout << DLOG_COLOR_WARN;
            break;
        case LogLevel::ERROR:
            std::cout << DLOG_COLOR_ERROR;
            break;
        case LogLevel::FATAL:
            std::cout << DLOG_COLOR_FATAL;
            break;
    }
}
