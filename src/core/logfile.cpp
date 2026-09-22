#include "logfile.h"

#include "timestamp.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>


LogFile::LogFile(const std::string &basename, int64_t rollsize, int flushInterval, int checkEveryN)
    : m_basename(basename), m_rollsize(rollsize), m_flushInterval(flushInterval), m_checkEveryN(checkEveryN)
{
    // LogFile 构造时，m_file 还没有指向有效的当前日志文件对象。无论程序是首次启动还是进程重启，都需要先调用 rollFile() 初始化写入目标。rollFile() 会创建 FileUtil，并以追加方式创建或打开对应的日志文件，确保后续 append() 有可写的目标。
    rollFile();
}

void LogFile::append(const char *data, int len)
{
}

bool LogFile::rollFile()
{
    return true;
}

std::string LogFile::GetLogFileName(const std::string &basename, time_t &now)
{
    time_t currentTime = Timestamp::Now().secondsSinceEpoch();
    now = currentTime;

    // 同 Timestamp::toFormattedString() 函数。
    std::tm localTime{};

#if defined(D_OS_WIN32)
    if (::localtime_s(&localTime, &currentTime)) throw std::runtime_error("LogFile::GetLogFileName(): localtime_s failed");
#elif defined(D_OS_LINUX)
    if (!::localtime_r(&currentTime, &localTime)) throw std::runtime_error("LogFile::GetLogFileName(): localtime_r failed");
#else
    throw std::runtime_error("LogFile::GetLogFileName(): Unsupported Operating System");
#endif


    // std::put_time 是 C++ 风格的时间格式化方式，输出格式保持为：basename.YYYYmmdd-HHMMSS.log。
    return (std::ostringstream()
            << basename << '.'
            << std::put_time(&localTime, "%Y%m%d-%H%M%S")
            << ".log")
        .str();
}
