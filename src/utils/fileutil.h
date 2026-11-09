#ifndef _DLOG_FILEUTIL_H_
#define _DLOG_FILEUTIL_H_

#include "globalmacros.h"

#include "fixedbuffer.h"

#include <filesystem>


DLOG_NAMESPACE_BEGIN(dlog)


// 文件工具类，用于处理文件的写入操作。该类封装了对文件的基本操作，包括写入数据和刷新缓冲区。
class DLOG_API_EXPORTED FileUtil
{

    DLOG_CLASS_NONCOPYABLE(FileUtil)

public:

    // 以追加模式打开文件，并为文件设置内部缓冲区，主要面向高频、小块日志写入场景。为了减少每次写入都触发文件 I/O 的次数，专门为文件设置一个用户态缓冲区，先将多次 append() 的数据暂存到缓冲区，缓冲区写满或显式调用 flush() 时再批量提交。
    // 相对路径以当前进程可执行文件目录为基准，并做词法规范化；绝对路径直接使用。自动创建缺失的父目录；父目录路径被非目录占用或创建失败时抛出 std::filesystem::filesystem_error。
    explicit FileUtil(const std::filesystem::path &filePath);

    // 析构函数。刷新并关闭文件。
    ~FileUtil();

    // 追加数据。发生流写入错误时抛出 std::ios_base::failure，保留已被文件流接收的字节。
    void append(const char *data, size_t len);

    // 将用户态缓冲区中的数据刷新到文件。
    void flush();

    // 返回已经成功写入的字节数。
    int64_t writtenBytes() const noexcept { return m_writtenBytes; }


    // 返回当前进程可执行文件的绝对路径，包含文件名，不受工作目录影响；获取失败抛出 std::system_error（包括派生异常）。
    static std::filesystem::path executableFullPath();

    // 返回可执行文件所在目录。
    static std::filesystem::path executableDirectory() { return executableFullPath().parent_path(); }


private:

    // 执行一次底层写入操作。append() 负责处理部分写入。
    size_t write(const char *data, size_t len) noexcept;


    // 文件指针，用于操作文件。
    FILE *m_file = nullptr;

    // 仅借出固定的 64 KiB 存储供 FILE* 使用，不通过 FixedBuffer 维护文件流的有效数据长度。
    FixedBuffer<64 * 1024> m_buffer;

    // 记录已写入文件的总字节数，int64_t 类型用于大文件支持。
    int64_t m_writtenBytes = 0;
};


DLOG_NAMESPACE_END


#endif
