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

    // 写入并刷新。
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

    FileFlush(const std::string &basePath, int64_t rollSize, unsigned int flushInterval = 3);

    ~FileFlush() override = default;

    void writeAndFlush(const char *data, size_t len) override;


private:

    std::unique_ptr<LogFile> m_file;
};


DLOG_NAMESPACE_END


#endif
