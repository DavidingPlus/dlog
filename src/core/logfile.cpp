#include "logfile.h"

#include "timestamp.h"

#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <stdexcept>


void LogFile::append(const char *data, int len)
{
    // TODO code review

    std::lock_guard<std::mutex> lock(m_mtx);

    if (len < 0) throw std::invalid_argument("LogFile::append(): len must not be negative");

    const time_t now = Timestamp::Now().secondsSinceEpoch();
    const std::string currentDate = GetDateString(now);

    // 每条日志通常来自一个临时 Logger，但它们共享同一个长期存在的 LogFile 后端。
    // 因此先判断当前日志应该属于哪个文件，再把它追加到当前 m_file。
    // 日期变化必须在写入前处理，否则跨天后的第一条日志仍会落到前一天的文件中。
    const bool dateChanged = currentDate != m_currentDate;
    const int64_t currentBytes = m_file->writtenBytes();
    const int64_t incomingBytes = static_cast<int64_t>(len);

    // 如果当前文件已有内容，并且追加这条日志后会超过大小限制，则提前切换文件。
    // 当前文件为空时，即使单条日志本身超过限制，也先完整写入，避免创建空的轮转文件。
    const bool sizeExceeded = currentBytes > 0 && incomingBytes > m_rollsize - currentBytes;

    // 大小和日期是两个独立的轮转条件，任一条件满足就只轮转一次。
    if (sizeExceeded || dateChanged) rollFileImpl(now, currentDate);

    m_file->append(data, static_cast<size_t>(len));

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

bool LogFile::rollFile()
{
    std::lock_guard<std::mutex> lock(m_mtx);

    time_t now = Timestamp::Now().secondsSinceEpoch();
    return rollFileImpl(now, GetDateString(now));
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

int LogFile::FindNextFileIndex(const std::string &basename, const std::string &date)
{
    // TODO code review

    const std::filesystem::path basenamePath(basename);
    const std::filesystem::path directory = basenamePath.has_parent_path() ? basenamePath.parent_path() : std::filesystem::path(".");
    const std::string prefix = basenamePath.filename().string() + '.' + date + '.';
    constexpr std::string_view suffix = ".log";

    std::error_code error;
    std::filesystem::directory_iterator entries(directory, error);
    if (error) throw std::filesystem::filesystem_error("LogFile::FindNextFileIndex", directory, error);

    int nextIndex = 0;
    for (const std::filesystem::directory_entry &entry : entries)
    {
        const std::string filename = entry.path().filename().string();
        if (filename.size() <= prefix.size() + suffix.size() || filename.compare(0, prefix.size(), prefix) != 0 ||
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0)
        {
            continue;
        }

        const std::string indexText = filename.substr(prefix.size(), filename.size() - prefix.size() - suffix.size());
        if (indexText.empty() ||
            !std::all_of(indexText.begin(), indexText.end(), [](unsigned char character)
                         { return std::isdigit(character) != 0; }))
        {
            continue;
        }

        int index = 0;
        const auto result = std::from_chars(indexText.data(), indexText.data() + indexText.size(), index);
        if (result.ec != std::errc{} || result.ptr != indexText.data() + indexText.size()) continue;
        if (index == std::numeric_limits<int>::max()) throw std::overflow_error("LogFile file index overflow");

        nextIndex = std::max(nextIndex, index + 1);
    }

    return nextIndex;
}

bool LogFile::rollFileImpl(time_t now, const std::string &date)
{
    // TODO code review

    const bool firstFile = !m_file;
    const bool dateChanged = date != m_currentDate;

    int nextIndex = m_fileIndex;
    if (firstFile || dateChanged)
    {
        // 首次创建或进入新日期时，扫描已有文件，选择当天最大已有序号之后的序号。
        nextIndex = FindNextFileIndex(m_basename, date);
    }
    else
    {
        // 同一天因大小超限轮转时，直接使用下一个序号。
        if (m_fileIndex == std::numeric_limits<int>::max()) throw std::overflow_error("LogFile file index overflow");
        nextIndex = m_fileIndex + 1;
    }

    const std::string filename = GetLogFileName(m_basename, date, nextIndex);

    // 先打开新文件，再替换旧的 FileUtil。新文件打开失败时，可以保留旧文件对象。
    auto newFile = std::make_unique<FileUtil>(filename);
    m_file = std::move(newFile);
    m_currentDate = date;
    m_fileIndex = nextIndex;
    m_lastFlush = now;
    return true;
}
