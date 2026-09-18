#include "fileutil.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>


namespace
{

    std::runtime_error makeFileError(const std::string &operation) { return std::runtime_error(std::string("FileUtil::") + operation + " failed: " + std::strerror(errno)); }

} // namespace


FileUtil::FileUtil(const std::string &fileName)
{
    // Linux 使用 e 标志设置 close-on-exec；Windows 使用二进制模式（b），避免换行符被转换。
#if defined(OS_WIN32)
    m_file = std::fopen(fileName.c_str(), "ab");
#elif defined(OS_LINUX)
    m_file = std::fopen(fileName.c_str(), "ae");
#endif

    if (!m_file) throw makeFileError("constructor");

    // std::setvbuf 必须在文件发生读写之前调用。
    if (std::setvbuf(m_file, m_buffer, _IOFBF, sizeof(m_buffer)) != 0)
    {
        std::fclose(m_file);
        m_file = nullptr;
        throw makeFileError("constructor");
    }
}

FileUtil::~FileUtil()
{
    if (m_file) std::fclose(m_file);
}

void FileUtil::append(const char *data, size_t len)
{
    if (0 == len) return;
    if (!data) throw std::invalid_argument("FileUtil::append(): data must not be null");

    size_t written = 0;

    // fwrite 允许部分写入，因此必须持续写到全部数据完成，或发生错误。
    while (written < len)
    {
        size_t remain = len - written;
        const size_t n = write(data + written, remain);

        if (n != remain)
        {
            if (std::ferror(m_file)) throw makeFileError("append");
            break;
        }

        written += n;
    }

    m_writtenBytes += static_cast<int64_t>(written);
}

void FileUtil::flush()
{
    if (std::fflush(m_file)) throw makeFileError("flush");
}

size_t FileUtil::write(const char *data, size_t len) noexcept
{
    return std::fwrite(data, 1, len, m_file);
}
