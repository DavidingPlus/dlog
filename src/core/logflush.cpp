#include "logflush.h"

#include "logfile.h"

#include <iostream>
#include <limits>
#include <stdexcept>


DLOG_NAMESPACE_BEGIN(dlog)


void StdoutFlush::write(const char *data, size_t len)
{
    if (0 == len) return;
    if (!data) throw std::invalid_argument("StdoutFlush::write(): data must not be null");
    // write 接收有符号长度，转换前检查，避免超大 len 被截断。
    if (len > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) throw std::length_error("StdoutFlush::write(): length exceeds streamsize");

    std::cout.write(data, static_cast<std::streamsize>(len));
    if (!std::cout) throw std::runtime_error("StdoutFlush::write() failed: stdout write error");
}

void StdoutFlush::flush()
{
    std::cout.flush();
    if (!std::cout) throw std::runtime_error("StdoutFlush::flush() failed: stdout flush error");
}

void FileFlush::write(const char *data, size_t len)
{
    if (0 == len) return;
    if (!data) throw std::invalid_argument("FileFlush::write(): data must not be null");

    m_file->append(data, len);
}


DLOG_NAMESPACE_END
