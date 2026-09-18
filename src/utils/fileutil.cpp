#include "fileutil.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>


namespace
{

    std::runtime_error makeFileError(const char *operation)
    {
        return std::runtime_error(std::string("FileUtil::") + operation + " failed: " + std::strerror(errno));
    }

} // namespace


FileUtil::FileUtil(const std::string &fileName)
{
    // Linux 使用 e 标志设置 close-on-exec；Windows 使用二进制模式，避免换行符被转换。
#if defined(OS_WIN32)
    m_file = std::fopen(fileName.c_str(), "ab");
#elif defined(OS_LINUX)
    m_file = std::fopen(fileName.c_str(), "ae");
#endif

    if (m_file == nullptr) throw makeFileError("constructor");

    // setvbuf 必须在文件发生读写之前调用。
    if (std::setvbuf(m_file, m_buffer, _IOFBF, sizeof(m_buffer)) != 0)
    {
        std::fclose(m_file);
        m_file = nullptr;
        throw makeFileError("constructor");
    }
}


FileUtil::~FileUtil()
{
    if (m_file != nullptr) std::fclose(m_file);
}


void FileUtil::append(const char *data, std::size_t len)
{
    if (len == 0) return;
    if (data == nullptr) throw std::invalid_argument("FileUtil::append data must not be null");

    std::size_t written = 0;

    // fwrite 允许部分写入，因此必须持续写到全部数据完成，或发生错误。
    while (written < len)
    {
        const std::size_t n = write(data + written, len - written);

        if (n == 0)
        {
            if (std::ferror(m_file) != 0) throw makeFileError("append");
            break;
        }

        written += n;
    }

    m_writtenBytes += static_cast<std::int64_t>(written);
}


void FileUtil::flush()
{
    if (std::fflush(m_file) != 0) throw makeFileError("flush");
}


std::size_t FileUtil::write(const char *data, std::size_t len) noexcept
{
    return std::fwrite(data, 1, len, m_file);
}
