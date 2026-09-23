#include "logcolor.h"

#include "logger.h"


LogColorGuard::LogColorGuard(Logger::LogLevel level)
{
    switch (level)
    {
        case Logger::LogLevel::INFO:
            std::cout << LOG_COLOR_INFO;
            break;
        case Logger::LogLevel::ERROR:
            std::cout << LOG_COLOR_ERROR;
            break;
        case Logger::LogLevel::FATAL:
            std::cout << LOG_COLOR_FATAL;
            break;
        case Logger::LogLevel::DEBUG:
            std::cout << LOG_COLOR_DEBUG;
            break;
    }
}
