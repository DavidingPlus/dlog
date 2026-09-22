#ifndef _DLOG_LOGGER_H_
#define _DLOG_LOGGER_H_

#include "logstream.h"
#include "timestamp.h"

#include <string_view>


// FileNameView 从路径中提取文件名，并以非拥有型视图的形式保存它。
class D_API_EXPORTED FileNameView
{

public:

    explicit FileNameView(const char *path);

    // 返回文件名视图，例如："Logger.cc"。
    const std::string_view &view() const noexcept { return m_view; }


private:

    // 生命周期说明：
    // 1. "abc" 是字符串字面量，类型是 const char[4]，其中还包含结尾的 '\0'。字符串字面量对应的字符数组具有静态存储期：程序启动后存在，直到程序结束。因此，FileNameView("abc") 中临时销毁的是 FileNameView 对象，而不是 "abc" 的字符数据；对象内部的视图仍然可以指向这段长期存在的字符数据。
    // 2. __FILE__ 是预定义宏，编译器会将它展开为当前源文件名对应的字符串字面量，例如："D:/Workspace/dlog/src/core/logger.cpp"。因此 Logger 使用 __FILE__ 构造 FileNameView 时，m_view 指向的字符数据具有静态存储期，生命周期是安全的。
    // 3. std::string_view 本身不拥有字符串，只保存字符数据的起始地址和长度。因此，path 指向的字符数据必须比当前 FileNameView 对象存活更久。下面的用法是不安全的：FileNameView view(std::string("logger.cpp").c_str()); 临时 std::string 会在这条完整表达式结束时销毁，c_str() 返回的地址随之失效，view 内部就会变成悬空视图。局部 std::string 的 c_str() 也只能在原 string 仍然存活且没有触发可能导致重新分配的修改时使用。
    // 4. 这里使用 std::string_view 是因为 Logger 的输入来源是 __FILE__，不需要复制文件名。如果未来允许保存任意来源、且无法保证调用方生命周期的字符串，就应该改用 std::string，让类拥有一份数据，从而避免悬空视图。
    std::string_view m_view;
};


class D_API_EXPORTED Logger
{

public:

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


private:

    class LoggerImpl
    {

    public:

        LoggerImpl(LogLevel level, int savedErrno, const char *filename, int line);

        void formatTime();

        // 添加一条 log 消息的后缀。
        void finish();


        Timestamp m_time;

        LogStream m_stream;

        LogLevel m_level;

        FileNameView m_basename;

        int m_line;
    };


    LoggerImpl m_impl;
};


#endif
