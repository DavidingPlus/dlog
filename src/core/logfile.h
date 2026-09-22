#ifndef _DLOG_LOGFILE_H_
#define _DLOG_LOGFILE_H_

#include "logstream.h"
#include "fileutil.h"

#include <mutex>
#include <memory>


class LogFile
{

public:

    LogFile(const std::string &basename, int64_t rollsize, int flushInterval = 3, int checkEveryN = 1024);

    ~LogFile();

    void append(const char *data, int len);

    void flush();

    bool rollFile();


private:


    static std::string GetLogFileName(const std::string &basename, time_t *now);


    void appendInlock(const char *data, int len);


    static constexpr int kRollPerSeconds = 60 * 60 * 24;


    const std::string m_basename;

    // 滚动文件大小。
    const int64_t m_rollsize = 0;

    // 冲刷时间限值，默认 3s。
    const int m_flushInterval = 0;

    // 写数据次数限制，默认 1024。
    const int m_checkEveryN = 0;

    // 写数据次数计数, 超过限值 m_checkEveryN 时清除, 然后重新计数。
    int m_count = 0;

    std::mutex m_mutex;

    // 本次写 log 周期的起始时间（秒）。
    time_t m_startOfPeriod = 0;

    // 上次 roll 日志文件时间（秒）。
    time_t m_lastRoll = 0;

    // 上次 flush 日志文件时间（秒）。
    time_t m_lastFlush = 0;

    std::unique_ptr<FileUtil> m_file;
};


#endif
