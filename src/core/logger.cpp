#include "logger.h"


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
{
}

void Logger::LoggerImpl::formatTime()
{
}

void Logger::LoggerImpl::finish()
{
}
