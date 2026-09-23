#include "logfile.h"

#include "timestamp.h"

#include <filesystem>
#include <iomanip>
#include <limits>
#include <string_view>
#include <stdexcept>


namespace
{

    // 尝试从文件名中解析日志序号。
    // 例如 filename 为 "app.20260923.12.log"，prefix 为 "app.20260923."，suffix 为 ".log" 时，函数会解析出 index == 12。文件名结构不匹配、序号为空、序号包含非数字字符或超出 int 范围时返回 false。
    // std::numeric_limits<int>::max() == index 仍然属于合法序号；是否还能继续生成下一个序号由调用方负责判断。
    bool tryParseLogFileIndex(const std::string &filename, const std::string_view &prefix, const std::string_view &suffix, int &index)
    {
        // 先检查长度、前缀和后缀，保证下面计算序号区间时不会越界。
        if (filename.size() <= prefix.size() + suffix.size() || filename.compare(0, prefix.size(), prefix) != 0 ||
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) return false;

        // 序号位于 prefix 和 suffix 之间，使用指针区间避免创建 substr 临时字符串。
        const char *indexBegin = filename.data() + prefix.size();
        const char *indexEnd = filename.data() + filename.size() - suffix.size();

        // from_chars() 对有符号整数允许负号，而日志序号只接受非负十进制数字，因此先检查首字符。
        // 后续字符是否全部被消费由 res.ptr == indexEnd 保证。
        if (indexBegin == indexEnd || *indexBegin < '0' || *indexBegin > '9') return false;

        auto res = std::from_chars(indexBegin, indexEnd, index);


        return res.ec == std::errc{} && res.ptr == indexEnd;
    }

} // namespace


void LogFile::append(const char *data, size_t len)
{
    if (0 == len) return;

    std::lock_guard<std::mutex> lock(m_mtx);

    time_t now = Timestamp::Now().secondsSinceEpoch();
    std::string currentDate = GetDateString(now);

    // 处理策略见函数声明。

    // 处理日期。
    bool dateChanged = currentDate != m_currentDate;
    // 处理大小。
    bool sizeExceeded = m_file->writtenBytes() > 0 && static_cast<int64_t>(len) + m_file->writtenBytes() > m_rollsize;

    // 大小和日期是两个独立的轮转条件，任一条件满足就只轮转一次。
    if (sizeExceeded || dateChanged) rollFileImpl(now, currentDate);

    m_file->append(data, len);

    // flush 与轮转相互独立：即使本次没有轮转，只要达到时间间隔也要刷新当前文件。
    if (now - m_lastFlush >= m_flushInterval)
    {
        m_lastFlush = now;
        m_file->flush();
    }
}

void LogFile::flush()
{
    std::lock_guard<std::mutex> lock(m_mtx);

    m_file->flush();
}

void LogFile::rollFile()
{
    std::lock_guard<std::mutex> lock(m_mtx);

    time_t now = Timestamp::Now().secondsSinceEpoch();
    rollFileImpl(now, GetDateString(now));
}

std::string LogFile::GetDateString(time_t time)
{
    // 同 Timestamp::toFormattedString() 函数。
    std::tm localTime{};

#if defined(D_OS_WIN32)
    if (::localtime_s(&localTime, &time)) throw std::runtime_error("LogFile::GetDateString(): localtime_s failed");
#elif defined(D_OS_LINUX)
    if (!::localtime_r(&time, &localTime)) throw std::runtime_error("LogFile::GetDateString(): localtime_r failed");
#else
    throw std::runtime_error("LogFile::GetDateString(): Unsupported Operating System");
#endif


    return (std::ostringstream() << std::put_time(&localTime, "%Y%m%d")).str();
}

int LogFile::FindNextFileIndex(const std::string &basePath, const std::string &date)
{
    // 1. 把 basePath 拆成“扫描目录”和“文件名前缀”。
    // 例如 basePath 为 "logs/app"、date 为 "20260923" 时：directory == "logs"，prefix == "app.20260923."，suffix == ".log"，最终只匹配 "app.20260923.<数字>.log"。
    std::filesystem::path basePathObj(basePath);
    std::filesystem::path directory = basePathObj.has_parent_path() ? basePathObj.parent_path() : std::filesystem::path(".");
    std::string prefix = basePathObj.filename().string() + '.' + date + '.';
    std::string_view suffix = ".log";

    // 2. 打开日志所在目录。
    // 使用带 error_code 的构造函数，把目录不存在、无权限等错误转换成带路径上下文的异常。
    std::error_code error;
    std::filesystem::directory_iterator entries(directory, error);
    if (error) throw std::filesystem::filesystem_error("LogFile::FindNextFileIndex", directory, error);

    // 3. 扫描所有目录项，维护“最大序号加一”。
    // 注：这里返回的不是最小空缺序号。例如已有 .0 和 .2 时返回 3，避免重启后复用旧序号。
    int nextIndex = 0;
    for (auto &entry : entries)
    {
        // 4. 委托给解析函数筛选文件名并提取序号。
        int index = 0;
        if (!tryParseLogFileIndex(entry.path().filename().string(), prefix, suffix, index)) continue;

        // std::numeric_limits<int>::max() 是合法的现有序号，但无法再生成 index + 1，因此不能继续轮转。
        if (std::numeric_limits<int>::max() == index) throw std::overflow_error("LogFile::FindNextFileIndex(): LogFile file index overflow");

        // 维护最大序号加一的返回值。
        nextIndex = std::max(nextIndex, 1 + index);
    }


    return nextIndex;
}

void LogFile::rollFileImpl(time_t now, const std::string &date)
{
    bool firstFile = !m_file;
    bool dateChanged = date != m_currentDate;

    int nextIndex = 0;
    // 首次创建或日期变化。
    if (firstFile || dateChanged)
    {
        // 扫描已有文件，选择当天最大已有序号之后的序号。
        nextIndex = FindNextFileIndex(m_basePath, date);
    }
    // 否则是大小超限。
    else
    {
        if (std::numeric_limits<int>::max() == m_fileIndex) throw std::overflow_error("LogFile::rollFileImpl(): LogFile file index overflow");
        // 使用下一个序号。
        nextIndex = 1 + m_fileIndex;
    }

    std::string filename = GetLogFileName(m_basePath, date, nextIndex);

    // 先打开新文件，再替换旧的 FileUtil。新文件打开失败时，可以保留旧文件对象。
    auto newFile = std::make_unique<FileUtil>(filename);
    m_file = std::move(newFile);

    m_currentDate = date;
    m_fileIndex = nextIndex;
    m_lastFlush = now;
}
