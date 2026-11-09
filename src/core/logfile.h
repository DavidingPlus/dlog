#ifndef _DLOG_LOGFILE_H_
#define _DLOG_LOGFILE_H_

#include "logstream.h"
#include "fileutil.h"
#include "timestamp.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sstream>


DLOG_NAMESPACE_BEGIN(dlog)


// LogFile 是长期存在的文件后端：普通模式始终追加到指定文件，轮转模式在大小超限或本地日期变化时切换文件。
// 轮转模式的文件名采用“日期 + 当天序号”的格式：
//   app.20260922.0.log  // 15:30:12 创建，当天第一个文件。
//   app.20260922.1.log  // 同一天文件大小超限后创建。
//   app.20260922.2.log  // 同一天再次大小超限后创建。
//   app.20260923.0.log  // 日期变化后，序号重新从 0 开始。
// 程序重启时会扫描当天已有的序号。例如已经存在 .0 和 .1，就从 .2 开始，避免覆盖旧日志。
class DLOG_API_EXPORTED LogFile
{

    DLOG_CLASS_NONCOPYABLE(LogFile)

public:

    // 普通模式。按完整文件名追加，不添加日期、序号，也不自动轮转；FileUtil 会自动创建缺失的父目录。
    explicit LogFile(const std::filesystem::path &filename) : m_file(std::make_unique<FileUtil>(filename)) {}

    // 轮转模式。构造时 m_file 还没有指向有效文件对象。rollFile() 会根据 basePath 扫描当天已有序号，选择下一个可用序号并创建当前文件，避免程序重启时覆盖旧日志。
    LogFile(const std::filesystem::path &basePath, int64_t rollSize);

    ~LogFile() = default;

    // 追加数据，不主动刷新；刷新时机由调用方决定。以下规则只适用于轮转模式：
    // 1. 日期变化时，先切换到新日期的日志文件，再写入本次数据，避免跨天后的日志继续写入前一天的文件。
    // 2. 如果当前文件已有内容，且写入本次数据后会超过 rollSize，则先切换到下一个序号的文件，再写入本次数据。写入后刚好等于 rollSize 时不轮转，允许文件达到但不超过大小限制。
    // 3. 如果当前文件为空，即使本次数据本身大于 rollSize，也直接完整写入当前文件。这是“保持一次 append 数据完整”和“严格限制单个文件大小”之间的取舍：允许这一个文件超限，以避免先创建一个空的轮转文件，也避免拆分一段日志数据。
    // 4. 日期轮转和大小轮转同时满足时只轮转一次；新文件同时满足新的日期和序号条件。
    // 5. len 使用 size_t 类型，调用方应保证传入的数据范围有效。len == 0 不会追加任何数据。
    void append(const char *data, size_t len);

    // 将当前日志文件对象内部缓冲区的数据刷新到当前文件，不会创建或切换日志文件。
    void flush();

    // 创建并切换到下一个轮转文件。普通模式调用会抛出 std::logic_error，避免改变指定文件名。
    void rollFile();


private:

    struct RollingState
    {
        // 将 time_t 转换为本地日期字符串，格式为 YYYYMMDD，例如：20260922。
        static std::string GetDateString(time_t time);

        // 根据基础路径生成带日期和序号的日志文件路径。例如 basePath 为 "logs/app" 时，生成 "logs/app.20260922.0.log"。
        static std::filesystem::path GetLogFileName(const std::filesystem::path &basePath, const std::string &date, int fileIndex);

        // 扫描 basePath 所在目录中当天已有的日志文件，返回最大合法序号加一。例如当天已有 .0 和 .1，则返回 2；没有已有文件时返回 0。
        static int FindNextFileIndex(const std::filesystem::path &basePath, const std::string &date);


        // 轮转模式的基础路径，不是单独的目录，由“目录路径 + 基础文件名”组成，后续会在其后追加日期、序号和 .log 后缀。
        // 构造时将相对路径解析到可执行文件目录并保存为绝对路径，保证扫描和写入使用同一目录，后续切换工作目录也不影响轮转。缺失的父目录会在轮转扫描前自动创建（见 FindNextFileIndex() 函数）。
        std::filesystem::path m_basePath;

        // 单个日志文件允许达到的大小，单位是字节。下一次追加会超过该大小时，在写入前创建新文件。
        int64_t m_rollSize = 0;

        // 当前文件所属的本地日期，例如：20260922。日期变化时会为新日期选择下一个可用序号。
        std::string m_currentDate;

        // 当前日期下正在使用的文件序号，例如 app.20260922.1.log 中的 1。
        int m_fileIndex = 0;
    };


    // 执行实际的轮转逻辑，要求 m_rollingState 有值且调用方已持锁；这是不加锁的内部版本。
    // append() 发现需要轮转时已经持有 m_mtx，不能再次调用同样会加锁的公共 rollFile()，否则同一线程会重复锁定 std::mutex 并发生死锁。
    void rollFileImpl(const std::string &date);


    // 两种模式共用。保护当前文件及可选轮转状态 RollingState，串行执行追加、flush 和轮转。
    mutable std::mutex m_mtx;

    // 两种模式共用。当前接收日志的文件，只有轮转时才替换为新的 FileUtil。
    std::unique_ptr<FileUtil> m_file;

    // std::optional 在对象内部预留 RollingState 的存储，并记录是否已构造该值，不需要额外的堆分配。
    // 普通模式默认无值，has_value() 返回 false。轮转模式构造时传入 RollingState，has_value() 返回 true。通过“是否有值”同时表示文件模式，无需另设布尔标志。
    // 通过 -> 访问成员前须保证有值，-> 本身不检查。
    std::optional<RollingState> m_rollingState;
};


DLOG_NAMESPACE_END


#endif
