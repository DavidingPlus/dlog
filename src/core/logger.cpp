#include "logger.h"

#include <cstdio>


DLOG_NAMESPACE_BEGIN(dlog)


namespace
{

    // 默认的日志输出函数，将日志内容写入标准输出流（stdout）。
    void defaultOutput(const char *data, size_t len) { std::fwrite(data, len, sizeof(char), stdout); }

    // 默认的刷新函数，刷新标准输出流的缓冲区，确保日志及时输出，在发生错误或需要立即看到日志时会被调用。
    void defaultFlush() { std::fflush(stdout); }

    Logger::OutputFunc g_outputCallback = defaultOutput;

    Logger::FlushFunc g_flushCallback = defaultFlush;

    LogLevelColorMode g_colorMode = LogLevelColorMode::ON;

} // namespace


Logger::Logger(const char *filename, int line, LogLevel level, int savedErrno)
    : m_message(filename, line, level, savedErrno, g_outputCallback, g_flushCallback, g_colorMode) {}

void Logger::SetOutput(OutputFunc output, LogLevelColorMode colorMode)
{
    g_outputCallback = output;
    g_colorMode = colorMode;
}

void Logger::SetFlush(FlushFunc flush) { g_flushCallback = flush; }


DLOG_NAMESPACE_END
