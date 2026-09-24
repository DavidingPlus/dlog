#ifndef _DLOG_LOGGER_H_
#define _DLOG_LOGGER_H_

#include "logstream.h"
#include "timestamp.h"

#include <string_view>
#include <functional>


// 日志宏采用类似 Qt qDebug() 的函数式调用方式：DLOG_INFO() << "server started" << port; 宏展开为临时 Logger 的 LogStream 引用，并捕获宏调用处的文件名和行号，当前完整表达式结束后，临时 Logger 析构并输出整条日志。
// 系统错误日志由调用方传入已经保存的 errno；应在确认系统调用失败后立即保存，避免后续调用改写 errno。示例：const int savedErrno = errno; DLOG_SYS_ERROR(savedErrno) << "open file failed";
#define DLOG_TRACE() (Logger(__FILE__, __LINE__, LogLevel::TRACE).stream())
#define DLOG_DEBUG() (Logger(__FILE__, __LINE__, LogLevel::DEBUG).stream())
#define DLOG_INFO() (Logger(__FILE__, __LINE__, LogLevel::INFO).stream())
#define DLOG_WARN() (Logger(__FILE__, __LINE__, LogLevel::WARN).stream())
#define DLOG_ERROR() (Logger(__FILE__, __LINE__, LogLevel::ERROR).stream())
#define DLOG_FATAL() (Logger(__FILE__, __LINE__, LogLevel::FATAL).stream())
#define DLOG_SYS_ERROR(savedErrno) (Logger(__FILE__, __LINE__, LogLevel::ERROR, (savedErrno)).stream())
#define DLOG_SYS_FATAL(savedErrno) (Logger(__FILE__, __LINE__, LogLevel::FATAL, (savedErrno)).stream())


// FileNameView 从路径中提取文件名，并以非拥有型视图的形式保存它。
class D_API_EXPORTED FileNameView
{

public:

    explicit FileNameView(const char *path);

    // 返回文件名视图，例如："Logger.cc"。
    std::string_view view() const noexcept { return m_view; }


private:

    // 生命周期说明：
    // 1. "abc" 是字符串字面量，类型是 const char[4]，其中还包含结尾的 '\0'。字符串字面量对应的字符数组具有静态存储期：程序启动后存在，直到程序结束。因此，FileNameView("abc") 中临时销毁的是 FileNameView 对象，而不是 "abc" 的字符数据；对象内部的视图仍然可以指向这段长期存在的字符数据。
    // 2. __FILE__ 是预定义宏，编译器会将它展开为当前源文件名对应的字符串字面量，例如："D:/Workspace/dlog/src/core/logger.cpp"。因此 Logger 使用 __FILE__ 构造 FileNameView 时，m_view 指向的字符数据具有静态存储期，生命周期是安全的。
    // 3. std::string_view 本身不拥有字符串，只保存字符数据的起始地址和长度。因此，path 指向的字符数据必须比当前 FileNameView 对象存活更久。下面的用法是不安全的：FileNameView view(std::string("logger.cpp").c_str()); 临时 std::string 会在这条完整表达式结束时销毁，c_str() 返回的地址随之失效，view 内部就会变成悬空视图。局部 std::string 的 c_str() 也只能在原 string 仍然存活且没有触发可能导致重新分配的修改时使用。
    // 4. 这里使用 std::string_view 是因为 Logger 的输入来源是 __FILE__，不需要复制文件名。如果未来允许保存任意来源、且无法保证调用方生命周期的字符串，就应该改用 std::string，让类拥有一份数据，从而避免悬空视图。
    std::string_view m_view;
};


// 日志等级。
enum class LogLevel
{
    TRACE,       // 最详细的跟踪信息。
    DEBUG,       // 调试信息。
    INFO,        // 普通运行信息。
    WARN,        // 警告，但程序通常还能继续运行。
    ERROR,       // 错误，需要处理。
    FATAL,       // 致命错误，通常会终止程序。
    LEVEL_COUNT, // 等级数量，不是真正的日志等级。
};


// 日志等级文字的颜色模式。
enum class LogLevelColorMode
{
    ON,  // 输出带颜色的日志等级。
    OFF, // 输出纯文本日志等级。
};


// Logger 负责一条日志消息的生命周期管理和元数据拼接，LogStream 负责具体的格式化与缓冲。
// 一条日志的典型执行流程是：
// 1. 创建 Logger 时，LoggerImpl 将时间、日志等级和源文件位置写入内部 LogStream 的前缀；
// 2. 调用 stream() 获取 LogStream，通过重载的 operator<< 将正文格式化后追加到固定缓冲区；
// 3. Logger 析构时调用 finish()，为日志追加换行符；
// 4. 析构函数通过 OutputFunc 将缓冲区中的有效字节写到 stdout 或调用方指定的输出位置。
// Logger 本身不负责打开或管理日志文件。默认输出回调写入 stdout；如果调用 SetOutput() 注册文件输出回调，则可以将同一条日志交给其他文件后端持久化。颜色由 SetOutput() 的 LogLevelColorMode 参数控制，默认 ON；文件后端需要纯文本时应传入 LogLevelColorMode::OFF。OutputFunc 接收 data 和 length 两个参数，因此缓冲区是“起始地址 + 有效长度”的字节序列，不保证以 '\0' 结尾，不能直接按 C 字符串处理。
// 典型的临时对象用法如下：Logger(__FILE__, __LINE__, LogLevel::INFO).stream() << "server started"; 当前完整表达式结束后，临时 Logger 析构并输出整条日志。若先保存为命名对象，则会在对象离开作用域时输出。FATAL 日志在输出后还会调用 FlushFunc 刷新输出，并终止进程；因此不应在普通单元测试中直接触发 FATAL。
class D_API_EXPORTED Logger
{

    D_CLASS_NONCOPYABLE(Logger)

public:

    // savedErrno 是调用方在系统错误发生时保存的 errno 快照；0 表示不附带系统错误信息。
    Logger(const char *filename, int line, LogLevel level, int savedErrno = 0) : m_impl(level, savedErrno, filename, line) {}

    ~Logger();

    // 返回当前日志的内部流。后续的 operator<< 只会修改本条日志自己的固定缓冲区，真正输出发生在 Logger 析构时。
    LogStream &stream() { return m_impl.m_stream; }

    // 输出函数。OutputFunc 使用 size_t 表示显式长度，因此调用方不需要、也不应该依赖 data 以 '\0' 结尾。
    using OutputFunc = std::function<void(const char *msg, size_t len)>;

    // 刷新缓冲区的函数。
    using FlushFunc = std::function<void()>;

    // 设置进程内共享的输出回调和颜色模式。默认启用颜色；应在开始产生日志前完成设置，避免运行期间并发修改配置，或在 Logger 对象存活期间切换配置。
    static void SetOutput(OutputFunc, LogLevelColorMode colorMode = LogLevelColorMode::ON);

    static void SetFlush(FlushFunc);


private:

    class LoggerImpl
    {

    public:

        LoggerImpl(LogLevel level, int savedErrno, const char *filename, int line);

        // 格式化一条 log 的时间部分。formatTime() 只修改 m_stream，不负责把缓冲区写入终端或日志文件。
        void formatTime();

        // 完成一条 log 消息：在已格式化的前缀和用户正文后追加换行符。同 formatTime()，也只修改 m_stream。
        void finish() { m_stream << '\n'; }


        // 日志创建时的时间戳。
        Timestamp m_time;

        // 日志的正文缓冲区。
        LogStream m_stream;

        // 日志的严重程度。
        LogLevel m_level;

        // 日志的源文件名，通常由 __FILE__ 宏传入。
        FileNameView m_basename;

        // 产生日志的源代码行号，通常由 __LINE__ 宏传入。它和 m_basename 组成调用位置，例如："[logger.cpp:42]"，方便定位日志是从哪里产生的。
        int m_line;
    };


    LoggerImpl m_impl;
};


#endif
