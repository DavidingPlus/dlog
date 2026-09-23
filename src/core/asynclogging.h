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

    AsyncLogging(const std::string &basePath, int64_t rollSize, int flushInterval = 3);

    ~AsyncLogging();

    // 前端线程追加一条已经格式化完成的日志数据。
    void append(const char *data, size_t length);

    // 启动和停止后台写盘线程。
    void start();

    void stop();


private:

    using LargeBuffer = FixedBuffer<kLargeBufferSize>;
    using BufferPtr = std::unique_ptr<LargeBuffer>;
    using BufferVector = std::vector<BufferPtr>;

    // 后台线程入口。
    void threadFunc();


    // 当前是否允许后台线程继续运行。
    std::atomic_bool m_running{false};

    // 日志文件配置。
    std::string m_basePath;
    int64_t m_rollSize = 0;
    int m_flushInterval = 0;

    // 后台写盘线程。
    Thread m_thread;

    // 保护前端缓冲区和待写缓冲区队列。
    std::mutex m_mutex;
    std::condition_variable m_condition;

    // 前端当前写入的缓冲区和备用缓冲区。
    BufferPtr m_currentBuffer;
    BufferPtr m_nextBuffer;

    // 已经交给后台线程、等待写盘的缓冲区队列。
    BufferVector m_buffers;
};


#endif
