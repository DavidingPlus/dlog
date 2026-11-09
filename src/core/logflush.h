#ifndef _DLOG_LOGFLUSH_H_
#define _DLOG_LOGFLUSH_H_

#include "globalmacros.h"

#include "logfile.h"

#include <memory>
#include <filesystem>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


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

    virtual void writeAndFlush(const char *data, size_t len) override;
};


class DLOG_API_EXPORTED FileFlush : public LogFlush
{

public:

    explicit FileFlush(const std::filesystem::path &filename) : m_file(std::make_unique<LogFile>(filename)) {}

    virtual ~FileFlush() override = default;

    virtual void writeAndFlush(const char *data, size_t len) override;


protected:

    FileFlush(const std::filesystem::path &basePath, int64_t rollSize) : m_file(std::make_unique<LogFile>(basePath, rollSize)) {}


private:

    std::unique_ptr<LogFile> m_file;
};


class DLOG_API_EXPORTED RollFileFlush : public FileFlush
{

public:

    RollFileFlush(const std::filesystem::path &basePath, int64_t rollSize) : FileFlush(basePath, rollSize) {}

    virtual ~RollFileFlush() override = default;
};


class DLOG_API_EXPORTED LogFlushFactory
{

public:

    template <typename LogFlushType, typename... Args>
    static std::shared_ptr<LogFlush> CreateLogFlush(Args &&...args) { return std::make_shared<LogFlushType>(std::forward<Args>(args)...); }
};


DLOG_NAMESPACE_END


#endif
