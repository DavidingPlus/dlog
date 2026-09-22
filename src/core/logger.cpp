#include "logger.h"

#include <array>
#include <cstring>
#include <mutex>


namespace
{

    // TODO 目前先用一把进程内互斥锁保护 std::strerror()。
    // std::strerror() 返回的字符指针可能指向 C 运行库内部的共享缓冲区，多个线程同时调用时，后一次调用可能覆盖前一次调用得到的错误信息。后续改用 strerror_r 或 strerror_s + thread_local 缓冲区后，这把锁可以移除。
    std::mutex g_errnoMutex;

    // LEVEL_COUNT 是等级数量，不属于实际日志等级，因此正好可以用来确定数组大小。
    // LogLevel 使用 enum class，不能直接拿枚举值作为数组下标，需要先转换为 size_t 类型。
    constexpr std::array<std::string_view, static_cast<size_t>(Logger::LogLevel::LEVEL_COUNT)> kLevelNames{
        "TRACE",
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR",
        "FATAL",
    };

    std::string_view levelName(Logger::LogLevel level) noexcept { return kLevelNames[static_cast<size_t>(level)]; }

} // namespace


FileNameView::FileNameView(const char *path)
    : m_view(path)
{
    // 同时处理 Unix 和 Windows 的路径分隔符。例如：2022/10/26/test.log 和 D:\Workspace\dlog\src\core\logger.h
    // find_last_of("/\\") 中的字符串表示“待查找的字符集合”：
    //   - /  ：Unix 路径分隔符；
    //   - \\ ：Windows 路径分隔符。这里的反斜杠需要额外转义。
    // 函数返回最后一个分隔符在 m_view 中的下标，而不是返回分隔符本身。
    auto sepPos = m_view.find_last_of("/\\");

    // npos 表示没有找到任何路径分隔符，此时 m_view 已经是文件名，不需要处理。
    // sepPos + 1 表示跳过最后一个 '/' 或 '\\'，使视图指向文件名首字符。
    // remove_prefix(n) 的语义是从当前视图的开头移除 n 个字符。它只调整视图的起始地址和长度，不修改、不复制、不移动底层字符数据。
    // 例如：对 "dir/log.txt" 调用 remove_prefix(4) 后，原字符串仍是 "dir/log.txt"，但当前视图变为 "log.txt"。
    // 调用者必须保证 n <= m_view.size()；这里的 sepPos + 1 正好指向文件名的首字符，因此满足这个前提。例如："D:/src/logger/Logger.cc" -> "Logger.cc"。
    if (std::string_view::npos != sepPos) m_view.remove_prefix(sepPos + 1);
}

Logger::LoggerImpl::LoggerImpl(Logger::LogLevel level, int savedErrno, const char *filename, int line)
    : m_time(Timestamp::Now()), m_level(level), m_basename(filename), m_line(line)
{
    // 根据时区格式化当前时间字符串, 也是一条 log 消息的开头，作为整条日志的前缀。
    formatTime();

    // 写入日志等级。
    m_stream << levelName(m_level) << ' ';

    // 如果调用方在进入 Logger 前保存了 errno，则把错误信息和 errno 数值一起写入正文前面。
    if (savedErrno)
    {
        // std::strerror(savedErrno) 返回的是字符指针，并不保证返回一份独立的字符串副本；这个指针可能指向 C 运行库内部的共享错误信息缓冲区。
        // 因此，锁必须一直持有到 m_stream 完成字符复制，执行顺序应当是：
        // 1. 加锁；
        // 2. 调用 strerror() 获取错误信息指针；
        // 3. 通过 m_stream << 立即复制错误信息；
        // 4. 解锁。
        // 如果在第 2 步之后提前解锁，其他线程可能调用 strerror() 并覆盖共享缓冲区，导致当前日志在第 3 步复制到错误的错误信息。

        std::lock_guard<std::mutex> lock(g_errnoMutex);

        // m_stream 属于当前 LoggerImpl，是当前日志独有的缓冲区，不是共享对象；这里的锁只保护 strerror() 返回的错误信息来源，不保护 m_stream 本身。
        m_stream << std::strerror(savedErrno) << " (errno=" << savedErrno << ") ";
    }
}

void Logger::LoggerImpl::formatTime()
{
    // m_time 在 LoggerImpl 构造时已经保存，是当前这条日志的时间戳。这里直接使用 m_time，而不是再次调用 Timestamp::Now()，这样可以避免一次额外的取时操作，并保证日志前缀表示 LoggerImpl 创建时的时间。

    // toFormattedString(true) 返回一个独立拥有字符数据的 std::string，例如："2026/09/22 15:30:12.123456"。LogStream 会在本次 operator<< 调用中把它复制到自己的固定缓冲区，因此临时字符串在这条语句结束后销毁不会造成悬空引用。
    // 多个线程分别格式化各自日志时，toFormattedString() 这条路径是线程安全的。但线程安全不等于没有开销：每条日志仍需要做时间格式化，并可能创建临时 std::string；如果后续日志频率很高，可以再使用 thread_local 缓存每秒不变的日期部分进行优化。
    m_stream << m_time.toFormattedString(true) << ' ';
}

void Logger::LoggerImpl::finish()
{
    // finish() 在日志对象生命周期结束时调用，负责补充调用位置和换行符，使缓冲区中的内容成为一条完整的日志。例如：2026/09/22 16:30:12.123456 INFO server started - main.cpp:42\n
    // m_basename.view() 返回的是非拥有型 std::string_view，LogStream 会在本次调用中立即把它复制到自己的固定缓冲区，因此这里只需要保证源文件名在 finish() 调用时仍有效。这里不执行真正的文件写入或 flush；后续由 Logger 的析构函数统一提交 m_stream 缓冲区。
    m_stream << " - " << m_basename.view() << ':' << m_line << '\n';
}
