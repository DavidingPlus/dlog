#ifndef _DLOG_ASYNCWORKER_H_
#define _DLOG_ASYNCWORKER_H_

#include "fixedbuffer.h"
#include "thread.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>


DLOG_NAMESPACE_BEGIN(dlog)


// AsyncWorker 只负责字节缓冲和后台批次处理，输出方式由回调决定，不承担日志记录格式化或文件配置。
class DLOG_API_EXPORTED AsyncWorker
{

    DLOG_CLASS_NONCOPYABLE(AsyncWorker)

public:

    // 后台按批次交付字节；数据仅在回调期间有效，接收方应在返回前处理完或自行复制。不得从回调中启停或销毁 Worker，避免后台线程 join 自身。
    // TODO 当前回调须不抛出异常，后台异常策略留待设计。
    using OutputFunc = std::function<void(const char *, size_t)>;


    // 保存输出回调并准备缓冲，不启动线程；flushInterval 为周期处理间隔，单位为秒。
    explicit AsyncWorker(OutputFunc output, unsigned int flushInterval = 3);

    // 停止并回收已启动的后台线程。
    ~AsyncWorker() { stop(); }

    // 将一段已经格式化的日志字节复制到 AsyncLogging 的内存缓冲区。
    // 使用互斥锁保护并发调用。若一条数据超过当前缓冲区的剩余空间，会按顺序分段写入；每块缓冲区写满后移入待写队列，并在释放互斥锁后唤醒后台线程，然后继续循环直到写完数据。
    void append(const char *data, size_t len);

    // 启停操作由调用方串行执行；运行中重复启动无效，正常停止后可再次启动。调用方应串行调用，不要并发调用 start() 和 stop()（一般也遇不到这种神人情况）。
    void start();

    // 未启动时为空操作；启动后正常停止会排空已接收数据并回收线程，重复调用安全。停止前应结束所有生产者。
    void stop();


private:

    // 缓冲区的独占所有权指针；队列交换和缓冲区轮换时移动指针即可。
    using BufferPtr = std::unique_ptr<LargeFixedBuffer>;

    // 待写缓冲区队列的动态数组类型。
    using BufferVector = std::vector<BufferPtr>;


    // 后台线程入口。
    void threadFunc();


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
};


DLOG_NAMESPACE_END


#endif
