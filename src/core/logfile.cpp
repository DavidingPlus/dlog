#include "logfile.h"


LogFile::LogFile(const std::string &basename, int64_t rollsize, int flushInterval, int checkEveryN)
    : m_basename(basename), m_rollsize(rollsize), m_flushInterval(flushInterval), m_checkEveryN(checkEveryN)
{
    // LogFile 构造时，当前日志文件对象还没有建立。无论程序是首次启动还是重新启动，都先调用 rollFile() 创建并打开当前日志文件，确保后续 append() 有可写的目标。
    rollFile();
}

void LogFile::append(const char *data, int len)
{
}

bool LogFile::rollFile()
{
    return true;
}

std::string LogFile::GetLogFileName(const std::string &basename, time_t *now)
{
    return {};
}
