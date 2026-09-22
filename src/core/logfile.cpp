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
    time_t now = 0;
    std::string filename = GetLogFileName(m_basename, now);

    if (now > m_lastRoll)
    {
        m_lastFlush = now;
        m_lastRoll = now;
        // now 是从 Unix 时间起点开始计算的秒数，kRollPerSeconds 表示一个日志周期的秒数（一天）。整除会得到当前属于第几个周期，乘回周期长度后得到这个周期的起始时间。例如：now = 3 * 86400 + 5 * 3600 时，start = 3 * 86400，表示第 3 天的起点。
        m_startOfPeriod = (now / kRollPerSeconds) * kRollPerSeconds;

        // 让 m_file 指向名为 filename 的文件。如果这个文件已经存在，FileUtil 以追加的方式打开该文件，不存在则创建。当前日志名字的命名规则下，精确到秒级，存在的概率不算大，所以大部分情况都是新建文件。
        m_file.reset(new FileUtil(filename));


        return true;
    }
    else
    {
        return false;
    }
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
