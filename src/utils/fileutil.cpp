#include "fileutil.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(DLOG_OS_WIN32)
#include <windows.h>
#endif


DLOG_NAMESPACE_BEGIN(dlog)


namespace
{

    std::runtime_error makeError(const std::string &operation) { return std::runtime_error(std::string("FileUtil::") + operation + " failed: " + std::strerror(errno)); }

} // namespace


FileUtil::FileUtil(const std::filesystem::path &filePath)
{
    if (filePath.empty()) throw std::invalid_argument("FileUtil::constructor(): path must not be empty");
    // C 文件接口以空字符结尾，拒绝内嵌空字符，避免实际打开的是路径前缀。
    if (std::filesystem::path::string_type::npos != filePath.native().find(std::filesystem::path::value_type{})) throw std::invalid_argument("FileUtil::constructor(): path must not contain null characters");

    // XXX path 的 / 运算符规则：右侧为绝对路径时，直接使用右侧绝对路径；右侧为相对路径时，拼接到左侧的可执行文件目录形成新绝对路径。
    auto absoluteFilePath = (executableDirectory() / filePath).lexically_normal();

    // 自动创建缺失的父目录，失败抛出 filesystem_error。
    std::filesystem::create_directories(absoluteFilePath.parent_path());

#if defined(DLOG_OS_WIN32)
    // Windows 直接使用 path 的宽字符路径，避免窄字符串转换丢失 Unicode；b 保留原始字节。
    m_file = ::_wfopen(absoluteFilePath.c_str(), L"ab");
#elif defined(DLOG_OS_LINUX)
    // Linux 使用原生字符路径，e 标志保留 close-on-exec 行为。
    m_file = std::fopen(absoluteFilePath.c_str(), "ae");
#endif

    if (!m_file) throw makeError("constructor");

    // 参考实现使用 ::setbuffer(m_file, m_buffer, sizeof(m_buffer)) 设置全缓冲。::setbuffer 是 Unix/BSD 扩展；当 m_buffer 非空时，它等价于：std::setvbuf(m_file, m_buffer, _IOFBF, sizeof(m_buffer));
    // 这里使用标准 C 接口 setvbuf，以兼容 Windows 和 Linux。_IOFBF 表示全缓冲，只有当缓冲区写满，或显式调用 flush()/析构关闭文件时，数据才会被刷新。setvbuf 必须在文件发生任何读写之前调用；m_buffer 是成员变量，生命周期覆盖文件使用期。
    if (std::setvbuf(m_file, m_buffer, _IOFBF, sizeof(m_buffer)))
    {
        std::fclose(m_file);
        m_file = nullptr;
        throw makeError("constructor");
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

    // append() 的语义是尽力将 len 个字节全部交给 FILE* 流。fwrite() 可能只接受其中一部分数据，因此每次写入后都根据实际返回值推进指针，直到全部完成或发生流错误。
    while (written < len)
    {
        size_t remain = len - written;
        size_t n = write(data + written, remain);
        written += n;

        if (n == remain) continue;

        if (std::ferror(m_file))
        {
            // ferror() 表示当前 FILE* 流发生了写入错误。已经交给流的部分无法回滚，因此当前追加只能放弃剩余数据；这里采用日志系统常见的“尽力记录”策略，不让日志写入异常继续影响业务线程。fwrite() 已经写入的部分无法回滚，因此保留实际接受的字节数。ferror() 只返回错误标志，不返回具体 errno，具体错误码需要从 errno 获取。
            std::cerr << "FileUtil::append() failed: " << ((0 != errno) ? std::error_code(errno, std::generic_category()).message() : "stream error") << '\n';

            // 清除 FILE* 的错误标志，使后续 append() 仍有机会重新尝试写入。clearerr() 只清除流状态，不会修复底层 I/O 错误，也不会恢复已经丢失的数据。
            std::clearerr(m_file);

            // 当前追加操作到此结束；剩余数据不再继续重试，避免在持续错误时陷入循环。
            break;
        }

        // 没有错误标志却没有任何写入进展时，继续重试会造成死循环，直接抛出异常。
        if (0 == n)
        {
            m_writtenBytes += static_cast<int64_t>(written);

            throw std::runtime_error("FileUtil::append() failed: incomplete write");
        }
    }

    m_writtenBytes += static_cast<int64_t>(written);
}

void FileUtil::flush()
{
    if (std::fflush(m_file)) throw makeError("flush");
}

std::filesystem::path FileUtil::executableFullPath()
{
#if defined DLOG_OS_WIN32

    // 使用宽字符支持中文路径，固定缓冲区覆盖 Windows 的长路径长度范围。
    constexpr const DWORD kMaxPathLength = 32768;
    wchar_t buffer[kMaxPathLength] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, kMaxPathLength);
    if (0 == length)
    {
        const DWORD error = GetLastError();
        throw std::system_error(static_cast<int>(error), std::system_category(), "executableFullPath(): GetModuleFileNameW() failed");
    }
    if (kMaxPathLength == length) throw std::system_error(ERROR_INSUFFICIENT_BUFFER, std::system_category(), "executableFullPath(): executable path was truncated");


    return std::filesystem::path(buffer, buffer + length);

#elif defined DLOG_OS_LINUX

    // /proc/self/exe 指向当前进程实际执行的文件，标准库负责读取完整的链接目标。
    return std::filesystem::read_symlink("/proc/self/exe");

#else

    throw std::system_error(std::make_error_code(std::errc::function_not_supported), "executableFullPath(): unsupported platform");

#endif
}

size_t FileUtil::write(const char *data, size_t len) noexcept
{
    // std::fwrite() 是标准 C 接口，会对 FILE* 流执行内部加锁，适合多线程环境下的流操作。Linux/Unix 的 ::fwrite_unlocked() 不执行这层加锁，单线程写入或外部已经完成同步时可能更快，但它不是标准 C++ 接口，Windows 对应的是 ::_fwrite_nolock()，因此这里选择 std::fwrite()。
    // 注意：std::fwrite() 只保护 FILE* 内部状态，并不能让整个 FileUtil::append() 变成线程安全；多个线程直接共享同一个 FileUtil 时，仍需在外部加锁。
    return std::fwrite(data, 1, len, m_file);
}


DLOG_NAMESPACE_END
