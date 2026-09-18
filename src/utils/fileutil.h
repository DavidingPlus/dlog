#ifndef _DLOG_FILEUTIL_H_
#define _DLOG_FILEUTIL_H_

#include "globalmacros.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>


// 文件工具类，用于处理文件的写入操作。该类封装了对文件的基本操作，包括写入数据和刷新缓冲区。
class FileUtil
{

    CLASS_NONCOPYABLE(FileUtil)

public:

    // 以追加模式打开文件，并为文件设置内部缓冲区。
    explicit FileUtil(const std::string &fileName);

    // 析构函数。刷新并关闭文件。
    ~FileUtil();

    // 向文件追加写入指定长度的数据。
    void append(const char *data, size_t len);

    // 将用户态缓冲区中的数据刷新到文件。
    void flush();

    // 返回已经成功写入的字节数。
    std::int64_t writtenBytes() const noexcept { return m_writtenBytes; }


private:

    // 执行一次底层写入操作。append() 负责处理部分写入。
    size_t write(const char *data, size_t len) noexcept;


    // 文件指针，用于操作文件。
    FILE *m_file = nullptr;

    // 文件操作的缓冲区，大小为 64 KB，用于提高写入效率。
    char m_buffer[64 * 1024]{};

    // 记录已写入文件的总字节数，int64_t 类型用于大文件支持。
    int64_t m_writtenBytes = 0;
};


#endif
