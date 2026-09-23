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

    AsyncLogging(const std::string &basePath, int64_t rollSize, int flushInterval = 3) : m_basePath(basePath), m_rollSize(rollSize), m_flushInterval(flushInterval), m_thread(std::bind(&AsyncLogging::threadFunc, this), "Logging"), m_currentBuffer(std::make_unique<LargeBuffer>()), m_nextBuffer(std::make_unique<LargeBuffer>()) { m_buffers.reserve(16); }

    ~AsyncLogging();

    // 前端线程追加一条已经格式化完成的日志数据。
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
    int m_flushInterval = 0;

    // 后台写盘线程，入口函数绑定到 threadFunc()。
    Thread m_thread;

    // 保护当前/备用缓冲区及待写队列，协调前台 append() 与后台批量取队列。
    std::mutex m_mutex;

    // 前台移交新批次后唤醒后台线程；后台线程也可配合定时等待。
    std::condition_variable m_cond;

    // 前台正在追加日志的缓冲区；空间不足时移入 m_buffers，再切换到备用缓冲区。
    BufferPtr m_currentBuffer;

    // 预备缓冲区，优先用于替换已移交的当前缓冲区，以减少运行中的动态分配。
    BufferPtr m_nextBuffer;

    // 前台已经填充并移交、等待后台写盘的缓冲区队列。
    // 构造函数中 reserve(16) 预留至少 16 个元素的容量，不限制队列长度。
    BufferVector m_buffers;
};


#endif
