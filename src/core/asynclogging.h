#ifndef _DLOG_ASYNCLOGGING_H_
#define _DLOG_ASYNCLOGGING_H_

#include "fixedbuffer.h"
#include "thread.h"

#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>


// AsyncLogging 将日志生产和日志写盘分离：前端线程只负责把日志追加到内存缓冲区，后台线程负责批量写入 LogFile。
class D_API_EXPORTED AsyncLogging
{

    D_CLASS_NONCOPYABLE(AsyncLogging)

public:

    AsyncLogging(const std::string &basePath, int64_t rollSize, unsigned int flushInterval = 3) : m_basePath(basePath), m_rollSize(rollSize), m_flushInterval(flushInterval), m_thread(std::bind(&AsyncLogging::threadFunc, this), "Logging"), m_producerBuffer(std::make_unique<LargeBuffer>()) { m_pendingBuffers.reserve(16); }

    ~AsyncLogging();

    // 将一段已经格式化的日志字节复制到 AsyncLogging 的内存缓冲区。
    // 使用互斥锁保护并发调用。若一条数据超过当前缓冲区的剩余空间，会按顺序分段写入；每块缓冲区写满后移入待写队列，并在释放互斥锁后唤醒后台线程，然后继续循环直到写完数据。
    void append(const char *data, size_t length);

    // 启动和停止后台写盘线程。
    void start();

    void stop();


private:

    // 单块日志缓冲区，容量在编译期由 kLargeBufferSize 固定。
    using LargeBuffer = FixedBuffer<kLargeBufferSize>;

    // 缓冲区的独占所有权指针；队列交换和缓冲区轮换时移动指针即可。
    using BufferPtr = std::unique_ptr<LargeBuffer>;

    // 待写缓冲区队列的动态数组类型。
    using BufferVector = std::vector<BufferPtr>;


    // 后台线程入口。
    void threadFunc();


    // 后台线程的运行标志。
    std::atomic_bool m_running{false};

    // 日志文件的基础路径。
    std::string m_basePath;

    // 单个日志文件的轮转阈值，单位为字节。
    int64_t m_rollSize = 0;

    // 后台定时等待/刷新的间隔，单位为秒。
    unsigned int m_flushInterval = 0;

    // 后台写盘线程，入口函数绑定到 threadFunc()。
    Thread m_thread;

    // 保护前台生产者缓冲区和待处理队列，协调 append() 与后台批量取队列。
    std::mutex m_mtx;

    // 前台移交新批次后唤醒后台线程；后台线程也可配合定时等待。
    std::condition_variable m_cond;

    // 前台生产者正在追加日志的缓冲区；写满后移入 m_pendingBuffers，并分配新缓冲区继续接收。
    BufferPtr m_producerBuffer;

    // 前台已经填充并移交、等待后台线程处理的缓冲区队列。
    BufferVector m_pendingBuffers;
};


#endif
