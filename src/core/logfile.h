#ifndef _DLOG_LOGFILE_H_
#define _DLOG_LOGFILE_H_

#include "logstream.h"
#include "fileutil.h"

#include <mutex>
#include <memory>


// LogFile 通常是一个长期存在的共享日志后端，而不是每条日志都重新创建的对象。Logger 可以在每条 LOG_INFO() 语句中临时创建，但最终都会把数据交给同一个 LogFile::append()；因此 m_file 会在一段时间内持续指向同一个日志文件，只有构造 LogFile 或发生轮转时才会切换文件。
// 例如，同一个 LogFile 对象运行期间：
// 1. 15:30:12 创建当前文件：app.20260922-153012.log
// 2. 16:00 写日志：仍然追加到 app.20260922-153012.log
// 3. 23:59 写日志：仍然追加到 app.20260922-153012.log
// 4. 次日第一次检查到跨天：调用 rollFile()
// 5. 00:00:05 切换到新文件：app.20260923-000005.log
class LogFile
{

public:

    LogFile(const std::string &basename, int64_t rollsize, int flushInterval = 3, int checkEveryN = 1024);

    ~LogFile() = default;

    void append(const char *data, int len);

    // 将当前日志文件对象内部缓冲区的数据刷新到当前文件。
    void flush() { m_file->flush(); }

    // 轮转日志文件：当当前文件达到大小限制或进入新的日志周期时，刷新并停止使用旧文件，然后创建并切换到新的日志文件。
    // 返回 true 表示完成了文件切换，返回 false 表示当前不满足轮转条件。
    bool rollFile();


private:


    // 生成日志文件名，格式为 basename.YYYYmmdd-HHMMSS.log，例如：app.20260922-153012.log。
    // now 是输出引用，返回生成文件名时的当前秒级时间戳。调用方用它更新 lastRoll、lastFlush 和 startOfPeriod 成员变量。
    static std::string GetLogFileName(const std::string &basename, time_t &now);


    // 一个日志周期包含的秒数。当前按一天计算，用于判断是否跨过日志周期。
    static constexpr int kRollPerSeconds = 60 * 60 * 24;


    // 保护当前文件、计数器和时间状态，保证多线程追加、flush 和轮转时不会相互冲突。
    std::mutex m_mtx;

    // 日志文件名的基本部分。例如 basename 为 "app" 时，完整文件名可以是 "app.20260922-153000.log"。
    std::string m_basename;

    // 当前正在接收日志数据的文件对象。普通 append() 会持续写入这个对象，发生轮转时才替换为新的 FileUtil。
    std::unique_ptr<FileUtil> m_file;

    // 单个日志文件允许达到的大小，单位是字节。写入后超过该大小时创建新文件。
    int64_t m_rollsize = 0;

    // 两次 flush() 之间允许经过的最长时间，单位是秒，默认 3 秒。
    int m_flushInterval = 0;

    // 每追加多少次日志后检查一次时间轮转，默认每 1024 次检查一次。
    int m_checkEveryN = 0;

    // 从上一次周期性检查后累计的追加次数。达到 m_checkEveryN 后清零并重新计数。
    int m_count = 0;

    // 当前日志周期的起始时间（秒）。通常表示当前 24 小时周期的起点。
    // 例如：now = 3 * 86400 + 5 * 3600 时，m_startOfPeriod = 3 * 86400，表示第 3 天的起点。
    time_t m_startOfPeriod = 0;

    // 上一次创建新日志文件的时间（秒），用于判断是否需要再次轮转。
    time_t m_lastRoll = 0;

    // 上一次将 FileUtil 缓冲区刷新到文件的时间（秒），用于判断是否达到 m_flushInterval。
    time_t m_lastFlush = 0;
};


#endif
