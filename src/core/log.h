#ifndef _DLOG_LOG_H_
#define _DLOG_LOG_H_

#include "logmanager.h"

#include <stdexcept>


DLOG_NAMESPACE_BEGIN(dlog)
DLOG_NAMESPACE_BEGIN(impl)

// 宏在调用方的编译单元展开，因此辅助函数的定义放在头文件中，让调用方无需从 DLL 导入该函数。impl 表示内部实现约定，并不阻止调用方直接访问。
// 使用具名命名空间中的外部链接 inline 函数，允许多个编译单元包含相同定义，并使宏在这些编译单元中引用同一实体；inline 不保证编译器一定展开调用。
// 若改用匿名命名空间，每个编译单元会有不同的函数；在多个编译单元共享的外部链接 inline 函数或模板中使用宏时，可能违反单一定义规则。
// 若改为头文件声明、.cpp 定义，函数需要外部链接，Windows DLL 构建还需导出该函数，才能供调用方链接。
inline AsyncLogger::Ptr checkLogger(AsyncLogger::Ptr logger)
{
    if (!logger) throw std::invalid_argument("dlog: logger must not be null");
    return logger;
}

DLOG_NAMESPACE_END
DLOG_NAMESPACE_END


// 日志宏采用类似 Qt qDebug() 的流式调用方式，例如 DLOG_INFO() << "server started" << port; 宏捕获调用处的文件名和行号，通过临时 LogMessage 的 LogStream 拼接内容，完整表达式结束时提交给异步日志器，由后台线程输出。
// 系统错误日志由调用方传入已经保存的 errno；应在确认系统调用失败后立即保存，避免后续调用改写 errno。例如：const int savedErrno = errno; DLOG_SYS_ERROR(savedErrno) << "open file failed";

#define DLOG_LOGGER_TRACE(logger) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::TRACE).stream())
#define DLOG_LOGGER_DEBUG(logger) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::DEBUG).stream())
#define DLOG_LOGGER_INFO(logger) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::INFO).stream())
#define DLOG_LOGGER_WARN(logger) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::WARN).stream())
#define DLOG_LOGGER_ERROR(logger) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::ERROR).stream())
#define DLOG_LOGGER_FATAL(logger) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::FATAL).stream())
#define DLOG_LOGGER_SYS_ERROR(logger, savedErrno) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::ERROR, (savedErrno)).stream())
#define DLOG_LOGGER_SYS_FATAL(logger, savedErrno) (::dlog::impl::checkLogger((logger))->message(__FILE__, __LINE__, ::dlog::LogLevel::FATAL, (savedErrno)).stream())

#define DLOG_TRACE() DLOG_LOGGER_TRACE(::dlog::LogManager::GetInstance().getDefaultLogger())
#define DLOG_DEBUG() DLOG_LOGGER_DEBUG(::dlog::LogManager::GetInstance().getDefaultLogger())
#define DLOG_INFO() DLOG_LOGGER_INFO(::dlog::LogManager::GetInstance().getDefaultLogger())
#define DLOG_WARN() DLOG_LOGGER_WARN(::dlog::LogManager::GetInstance().getDefaultLogger())
#define DLOG_ERROR() DLOG_LOGGER_ERROR(::dlog::LogManager::GetInstance().getDefaultLogger())
#define DLOG_FATAL() DLOG_LOGGER_FATAL(::dlog::LogManager::GetInstance().getDefaultLogger())
#define DLOG_SYS_ERROR(savedErrno) DLOG_LOGGER_SYS_ERROR(::dlog::LogManager::GetInstance().getDefaultLogger(), (savedErrno))
#define DLOG_SYS_FATAL(savedErrno) DLOG_LOGGER_SYS_FATAL(::dlog::LogManager::GetInstance().getDefaultLogger(), (savedErrno))


#endif
