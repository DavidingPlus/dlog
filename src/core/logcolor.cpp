#include "logcolor.h"

#include "logger.h"


LogColorGuard::LogColorGuard(LogLevel level)
{
    switch (level)
    {
        case LogLevel::INFO:
            std::cout << LOG_COLOR_INFO;
            break;
        case LogLevel::ERROR:
            std::cout << LOG_COLOR_ERROR;
            break;
        case LogLevel::FATAL:
            std::cout << LOG_COLOR_FATAL;
            break;
        case LogLevel::DEBUG:
            std::cout << LOG_COLOR_DEBUG;
            break;
    }
}
