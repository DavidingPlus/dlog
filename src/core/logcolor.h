#ifndef _DLOG_LOGCOLOR_H_
#define _DLOG_LOGCOLOR_H_

#include "globalmacros.h"

#include <iostream>

enum class LogLevel;


// 清除所有颜色，恢复到默认。
#define LOG_COLOR_RESET "\033[0m"

// 普通信息，绿色。
#define LOG_COLOR_INFO "\033[32m"

// 错误信息，红色。
#define LOG_COLOR_ERROR "\033[31m"

// 致命错误，粗体红色。
#define LOG_COLOR_FATAL "\033[41;97m"

// 调试信息，蓝色。
#define LOG_COLOR_DEBUG "\033[34m"


class D_API_EXPORTED LogColorGuard
{

public:

    // 构造时应用新传入等级对应的颜色。
    explicit LogColorGuard(LogLevel level);

    // 析构时自动恢复原始颜色。
    ~LogColorGuard() { std::cout << LOG_COLOR_RESET << std::flush; }
};


#endif
