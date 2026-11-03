#include "logflush.h"

#include "logfile.h"


DLOG_NAMESPACE_BEGIN(dlog)


void StdoutFlush::append(const char *, size_t)
{
}

void StdoutFlush::flush()
{
}

FileFlush::FileFlush(const std::string &, int64_t, unsigned int)
{
}

void FileFlush::append(const char *, size_t)
{
}

void FileFlush::flush()
{
}


DLOG_NAMESPACE_END
