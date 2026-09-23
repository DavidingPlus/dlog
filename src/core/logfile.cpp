#include "logfile.h"

#include "timestamp.h"

#include <filesystem>
#include <iomanip>
#include <limits>
#include <string_view>
#include <stdexcept>


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
