#ifndef _DLOG_LOGGER_H_
#define _DLOG_LOGGER_H_

#include "logworker.h"
#include "logflush.h"
#include "logmessage.h"

#include <atomic>
#include <memory>
#include <utility>


DLOG_NAMESPACE_BEGIN(dlog)


// 长期日志器，通过统一管理器 LogManager::buildLogger() 创建并启动 Worker。Manager 关闭时停止日志器，析构时释放其持有的引用。
class DLOG_API_EXPORTED Logger : public std::enable_shared_from_this<Logger>
{

    DLOG_CLASS_NONCOPYABLE(Logger)

    friend class LogManager;

public:

    using SharedPtr = std::shared_ptr<Logger>;

    ~Logger() = default;

    // 消息持有日志器，析构时提交。接受 TRACE 至 FATAL。
    LogMessage message(const char *filename, int line, LogLevel level, int savedErrno = 0);

    // 可选的主动等待：正常停止会排空，后台失败在回收线程后重抛；可重复调用，不提供重启。
    // 调用方必须先结束生产者与临时消息，再串行调用 stop；不得从输出回调中停止或销毁本对象。
    void stop();


private:

    explicit Logger(std::unique_ptr<LogWorker> worker);


    // 日志器是否正在运行。
    std::atomic_bool m_running{false};

    // Worker 持有输出目标并在析构时等待后台任务。
    std::unique_ptr<LogWorker> m_worker;
};


DLOG_NAMESPACE_END


#endif
