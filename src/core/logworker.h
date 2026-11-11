#ifndef _DLOG_LOGWORKER_H_
#define _DLOG_LOGWORKER_H_

#include "fixedbuffer.h"
#include "thread.h"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>


DLOG_NAMESPACE_BEGIN(dlog)


// LogWorker 只负责字节缓冲和后台批次处理，输出方式由回调决定，不承担日志记录格式化或文件配置。
class DLOG_API_EXPORTED LogWorker
{

    DLOG_CLASS_NONCOPYABLE(LogWorker)

public:

    // 后台按批次交付只读缓冲块。缓冲区由 LogWorker 管理，引用及数据仅在回调期间有效，接收方应在返回前处理完或自行复制。不得从回调中启停或销毁 Worker，避免后台线程 join 自身。
    using OutputFunc = std::function<void(const LargeFixedBuffer &)>;

    // 保存输出回调并准备缓冲，不启动线程；flushInterval 为周期处理间隔，单位为秒。
    explicit LogWorker(OutputFunc output, unsigned int flushInterval = 3);

    // 停止并回收已启动的后台线程；后台异常由显式 stop() 报告。
    ~LogWorker() { stopAndJoin(); }

    // 将一段已经格式化的日志字节复制到 Worker 的内存缓冲区。
    // 整次追加持有互斥锁，按顺序分段复制，写满的块移入待写队列；全部复制完成并释放锁后，若有完整块入队，只通知后台线程一次。后台失败后，非空追加重抛原异常。
    void append(const char *data, size_t len);

    // 启停由调用方串行执行；运行中重复启动无效，正常停止后可重启，后台失败后重抛原异常。
    void start();

    // 停止前应结束生产者。正常停止会排空，后台失败时放弃剩余数据并在线程回收后重抛原异常。
    void stop();


private:

    // 缓冲区的独占所有权指针；队列交换和缓冲区轮换时移动指针即可。
    using BufferPtr = std::unique_ptr<LargeFixedBuffer>;

    // 待写缓冲区队列的动态数组类型。
    using BufferVector = std::vector<BufferPtr>;


    // 后台线程入口。
    void threadFunc();

    void stopAndJoin();


    // 决定如何处理每个字节批次；Worker 不保存文件路径或轮转配置。
    OutputFunc m_output;

    // 后台线程的运行标志。置为 false 后仍需处理尾部数据并回收线程。
    std::atomic_bool m_running{false};

    // 未满缓冲块的周期处理间隔，单位为秒。
    unsigned int m_flushInterval = 3;

    // 后台写盘线程，入口函数绑定到 threadFunc()。
    Thread m_thread;

    // 保护前台生产者缓冲区和待处理队列，协调 append() 与后台批量取队列。
    mutable std::mutex m_mtx;

    // 前台移交新批次后唤醒后台线程；后台线程也可配合定时等待。
    std::condition_variable m_cond;

    // 前台生产者正在追加日志的缓冲区；写满后移入 m_pendingBuffers，并分配新缓冲区继续接收。
    BufferPtr m_producerBuffer;

    // 前台已经填充并移交、等待后台线程处理的缓冲区队列。
    BufferVector m_pendingBuffers;

    // 异常只沿当前线程的调用栈传播，逃出后台线程入口会触发 std::terminate。这里的设计是先保存后台异常，等 stop() 回收线程后，再在调用 stop() 的线程上重抛。若重抛后始终无人捕获，进程同样会终止。
    // 从目前来看，其实没有什么区别，只是维护了正确的语义，后续如果需要扩充的话可以正常让前台捕获后台抛出的异常，为正确处理留出了口子。调用方可在 stop() 外加 try/catch。析构只回收线程。
    // std::exception_ptr 默认构造为空，可用 if (m_backgroundError) 判断是否保存了异常。后台在 catch 中用 std::current_exception() 保存原异常，调用方判空后用 std::rethrow_exception(m_backgroundError) 按原异常类型重新抛出。
    std::exception_ptr m_backgroundError;
};


DLOG_NAMESPACE_END


#endif
