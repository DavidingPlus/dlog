#ifndef _DLOG_LOGFLUSH_H_
#define _DLOG_LOGFLUSH_H_

#include "globalmacros.h"

#include <memory>
#include <string>


DLOG_NAMESPACE_BEGIN(dlog)


class LogFile;


// 参考：https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/LogFlush.hpp
class DLOG_API_EXPORTED LogFlush
{

public:

    virtual ~LogFlush() = default;

    // 写入并刷新；len 为 0 时不操作。
    virtual void writeAndFlush(const char *data, size_t len) = 0;
};


class DLOG_API_EXPORTED StdoutFlush : public LogFlush
{

public:

    void writeAndFlush(const char *data, size_t len) override;
};


class DLOG_API_EXPORTED FileFlush : public LogFlush
{

public:

    // 文件模式由 LogFile 管理；两种模式都要求父目录已存在。
    explicit FileFlush(const std::string &filename);

    FileFlush(const std::string &basePath, int64_t rollSize);

    // LogFile 仅在此处前置声明，析构放到 .cpp 中定义。
    ~FileFlush() override;

    void writeAndFlush(const char *data, size_t len) override;


private:

    std::unique_ptr<LogFile> m_file;
};


DLOG_NAMESPACE_END


#endif
