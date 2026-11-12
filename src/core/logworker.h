#ifndef _DLOG_LOGWORKER_H_
#define _DLOG_LOGWORKER_H_

#include "flexiblebuffer.h"

#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>


DLOG_NAMESPACE_BEGIN(dlog)


class ThreadPool;


// LogWorker 只负责字节缓冲和后台批次处理，输出方式由回调决定，不承担日志记录格式化或文件配置。
class DLOG_API_EXPORTED LogWorker
{

    DLOG_CLASS_NONCOPYABLE(LogWorker)

    friend class Logger;

public:

    // 后台按批次交付只读缓冲块。缓冲区由 LogWorker 管理，引用及数据仅在回调期间有效，接收方应在返回前处理完或自行复制。不得从回调中启停或销毁 Worker，避免任务等待自身。
    using OutputFunc = std::function<void(const FlexibleBuffer &)>;

    // 调用方负责提供线程池，并在任务完成前保持线程池可用。
    explicit LogWorker(OutputFunc output, std::shared_ptr<ThreadPool> threadPool);

    // 等待已安排的任务；后台异常由显式 stop() 报告。
    ~LogWorker() { stopAndWait(); }

    // 将一段已经格式化的日志字节复制到 Worker 的内存缓冲区。整次追加持有互斥锁，按顺序复制到可增长缓冲区；启动后有非空数据就安排线程池处理。后台失败后，非空追加重抛原异常。
    void append(const char *data, size_t len);

    // 启停由调用方串行执行；运行中重复启动无效，正常停止后可重启，后台失败后重抛原异常。
    void start();

    // 停止前应结束生产者。正常停止会排空，后台失败时放弃剩余数据并在线程回收后重抛原异常。
    void stop();


private:

    // 缓冲区的独占所有权指针；交换指针即可交接生产者和消费者缓冲区。
    using BufferPtr = std::unique_ptr<FlexibleBuffer>;


    void scheduleTaskLocked();

    // 在线程池中按顺序处理待写缓冲块。
    void threadFunc();

    void stopAndWait();


    // 决定如何处理每个字节批次；Worker 不保存文件路径或轮转配置。
    OutputFunc m_output;

    // 日志输出任务使用的线程池。
    std::shared_ptr<ThreadPool> m_threadPool;

    // 保护前台生产者缓冲区和交换操作，协调 append() 与后台批量取数据。
    mutable std::mutex m_mtx;

    // Worker 是否已启动。
    bool m_running = false;

    // 是否已有输出任务排队或执行。
    bool m_scheduled = false;

    // 用于工作任务结束时唤醒等待停止的调用方。例如：线程池正在使用 Worker 的缓冲区和输出回调写日志，调用方调用 stop()。
    // 这时 stop() 不能马上返回，否则调用方可能接着销毁 Worker，而后台任务还在访问它。正确处理是：
    // 1. stop() 把 m_running 设为 false，然后等待 m_scheduled == false。
    // 2. wait() 期间释放 m_mtx，后台任务可以继续处理剩余数据。
    // 3. 后台处理完毕后，将 m_scheduled 设为 false，调用 notify_all()。
    // 4. stop() 被唤醒，确认条件成立后返回。
    std::condition_variable m_taskDoneCond;

    // 前台生产者正在追加日志的缓冲区；容量不足时按需增长。
    BufferPtr m_producerBuffer;

    // 后台处理当前批次的缓冲区；输出后清空并与生产者缓冲区轮换复用。
    BufferPtr m_consumerBuffer;

    // 异常只沿当前线程的调用栈传播，逃出后台任务（线程）入口会触发 std::terminate。这里的设计是先保存后台异常，等 stop() 回收线程后，再在调用 stop() 的线程上重抛。若重抛后始终无人捕获，进程同样会终止。任务提交失败也保存在这里，供后续调用报告。
    // 从目前来看，其实没有什么区别，只是维护了正确的语义，后续如果需要扩充的话可以正常让前台捕获后台抛出的异常，为正确处理留出了口子。调用方可在 stop() 外加 try/catch。析构只回收线程。
    // std::exception_ptr 默认构造为空，可用 if (m_backgroundError) 判断是否保存了异常。后台在 catch 中用 std::current_exception() 保存原异常，调用方判空后用 std::rethrow_exception(m_backgroundError) 按原异常类型重新抛出。
    std::exception_ptr m_backgroundError;
};


DLOG_NAMESPACE_END


#endif
