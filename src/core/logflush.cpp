#include "logflush.h"

#include "logfile.h"

#include <iostream>
#include <limits>
#include <stdexcept>


DLOG_NAMESPACE_BEGIN(dlog)


void StdoutFlush::writeAndFlush(const char *data, size_t len)
{
    if (0 == len) return;
    if (!data) throw std::invalid_argument("StdoutFlush::writeAndFlush(): data must not be null");
    // write 接收有符号长度，转换前检查，避免超大 len 被截断。
    if (len > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) throw std::length_error("StdoutFlush::writeAndFlush(): length exceeds streamsize");

    // 写入。
    std::cout.write(data, static_cast<std::streamsize>(len));
    if (!std::cout) throw std::runtime_error("StdoutFlush::writeAndFlush() failed: stdout write error");

    // 刷新。
    std::cout.flush();
    if (!std::cout) throw std::runtime_error("StdoutFlush::writeAndFlush() failed: stdout flush error");
}

FileFlush::FileFlush(const std::string &filename) : m_file(std::make_unique<LogFile>(filename))
{
}

FileFlush::FileFlush(const std::string &basePath, int64_t rollSize) : m_file(std::make_unique<LogFile>(basePath, rollSize))
{
}

FileFlush::~FileFlush() = default;

void FileFlush::writeAndFlush(const char *data, size_t len)
{
    if (0 == len) return;
    if (!data) throw std::invalid_argument("FileFlush::writeAndFlush(): data must not be null");

    // LogFile 只负责文件操作，这里统一完成一次写入和刷新。
    m_file->append(data, len);
    m_file->flush();
}


DLOG_NAMESPACE_END
