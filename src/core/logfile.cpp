#include "logfile.h"


// TODO
LogFile::LogFile(const std::string &basename, int64_t rollsize, int flushInterval, int checkEveryN)
{
}

LogFile::~LogFile()
{
}

void LogFile::append(const char *data, int len)
{
}

void LogFile::flush()
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

void LogFile::appendInlock(const char *data, int len)
{
}
