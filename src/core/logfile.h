#ifndef _DLOG_LOGFILE_H_
#define _DLOG_LOGFILE_H_

#include "logstream.h"
#include "fileutil.h"

#include <memory>
#include <mutex>
#include <string>
#include <sstream>


// LogFile 通常是一个长期存在的共享日志后端，而不是每条日志都重新创建的对象。Logger 可以在每条 LOG_INFO() 语句中临时创建，但最终都会把数据交给同一个 LogFile::append()。因此，普通 append() 会持续写入当前文件；只有文件大小超限或本地日期变化时才会轮转。
// 基础文件名采用“日期 + 当天序号”的格式：
//   app.20260922.0.log  // 15:30:12 创建，当天第一个文件。
//   app.20260922.1.log  // 同一天文件大小超限后创建。
//   app.20260922.2.log  // 同一天再次大小超限后创建。
//   app.20260923.0.log  // 日期变化后，序号重新从 0 开始。
// 程序重启时会扫描当天已有的序号。例如已经存在 .0 和 .1，就从 .2 开始，避免覆盖旧日志。
class D_API_EXPORTED LogFile
{

    D_CLASS_NONCOPYABLE(LogFile)

public:

    // 构造时 m_file 还没有指向有效文件对象。rollFile() 会根据 basePath 扫描当天已有序号，选择下一个可用序号并创建当前文件，避免程序重启时覆盖旧日志。
    LogFile(const std::string &basePath, int64_t rollsize, int flushInterval = 3) : m_basePath(basePath), m_rollsize(rollsize), m_flushInterval(flushInterval) { rollFile(); }

    ~LogFile() = default;

    // 追加一段日志数据。append() 将本次调用的数据视为不可拆分的写入单元，在真正写入前统一判断日期和文件大小是否需要轮转。
    // 1. 日期变化时，先切换到新日期的日志文件，再写入本次数据，避免跨天后的日志继续写入前一天的文件。
    // 2. 如果当前文件已有内容，且写入本次数据后会超过 m_rollsize，则先切换到下一个序号的文件，再写入本次数据。写入后刚好等于 m_rollsize 时不轮转，允许文件达到但不超过大小限制。
    // 3. 如果当前文件为空，即使本次数据本身大于 m_rollsize，也直接完整写入当前文件。这是“保持一次 append 数据完整”和“严格限制单个文件大小”之间的取舍：允许这一个文件超限，以避免先创建一个空的轮转文件，也避免拆分一段日志数据。
    // 4. 日期轮转和大小轮转同时满足时只轮转一次；新文件同时满足新的日期和序号条件。
    // 5. len 使用 size_t 类型，调用方应保证传入的数据范围有效。len == 0 不会追加任何数据。
    void append(const char *data, size_t len);

    // 将当前日志文件对象内部缓冲区的数据刷新到当前文件，不会创建或切换日志文件。
    void flush();

    // 创建并切换到下一个日志文件。构造时用于初始化当前文件，运行中用于响应大小或日期轮转。成功正常返回，失败抛出异常。
    void rollFile();


private:


    // 将 time_t 转换为本地日期字符串，格式为 YYYYMMDD，例如：20260922。
    static std::string GetDateString(time_t time);

    // 根据基础路径生成带日期和序号的日志文件路径。例如 basePath 为 "logs/app" 时，生成 "logs/app.20260922.0.log"。
    static std::string GetLogFileName(const std::string &basePath, const std::string &date, int fileIndex) { return (std::ostringstream() << basePath << '.' << date << '.' << fileIndex << ".log").str(); }

    // 扫描 basePath 所在目录中当天已有的日志文件，返回最大合法序号加一。例如当天已有 .0 和 .1，则返回 2；没有已有文件时返回 0。
    static int FindNextFileIndex(const std::string &basePath, const std::string &date);

    // 执行实际的轮转逻辑，但这是不加锁的内部版本。
    // append() 发现需要轮转时已经持有 m_mtx，不能再次调用同样会加锁的公共 rollFile()，否则同一线程会重复锁定 std::mutex 并发生死锁。
    void rollFileImpl(time_t now, const std::string &date);


    // 保护当前文件、日期、序号和 flush 状态，保证多线程追加、flush 和轮转时不会相互冲突。
    std::mutex m_mtx;

    // 日志文件的基础路径，不是单独的目录，由“目录路径 + 基础文件名”组成，后续会在其后追加日期、序号和 .log 后缀。
    // 可以使用绝对路径或相对路径。例如："D:/logs/app" 生成绝对路径 D:/logs/app.YYYYMMDD.0.log；"logs/app" 相对于当前工作目录生成日志，生成相对路径 "logs/app.20260922.0.log"；"app" 则表示当前工作目录下的日志文件，生成相对路径 "app.20260922.0.log"。
    // 父目录必须已经存在，LogFile 不负责创建目录。
    std::string m_basePath;

    // 当前正在接收日志数据的文件对象。普通 append() 会持续写入这个对象，发生轮转时才替换为新的 FileUtil。
    std::unique_ptr<FileUtil> m_file;

    // 单个日志文件允许达到的大小，单位是字节。下一次追加会超过该大小时，在写入前创建新文件。
    int64_t m_rollsize = 0;

    // 两次 flush() 之间允许经过的最长时间，单位是秒，默认 3 秒。
    int m_flushInterval = 0;

    // 当前文件所属的本地日期，例如：20260922。日期变化时会为新日期选择下一个可用序号。
    std::string m_currentDate;

    // 当前日期下正在使用的文件序号，例如 app.20260922.1.log 中的 1。
    int m_fileIndex = 0;

    // 上一次将 FileUtil 缓冲区刷新到文件的时间（秒），用于判断是否达到 m_flushInterval。
    time_t m_lastFlush = 0;
};


#endif
