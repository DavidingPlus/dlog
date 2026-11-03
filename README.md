# dlog

dlog 是一个基于 C++17 的轻量级日志库，支持同步输出、文件轮转和异步写盘。公共 C++ API 位于 `dlog` 命名空间中；`DLOG_*` 日志宏保留为全局宏。

## 功能

- 日志等级：提供 TRACE、DEBUG、INFO、WARN、ERROR 和 FATAL 六个等级。等级会显示在每条记录中；FATAL 记录输出后调用刷新回调并终止进程。
- 日志格式：每条记录包含本地时间（精确到微秒）、源文件名、代码行号和消息正文，格式为 `[时间] [等级] [文件名:行号] 消息`。源文件名从编译器提供的文件路径中提取。
- 消息拼接：日志宏采用流式写法，例如 `DLOG_INFO() << "request=" << requestId`。支持字符串、字符、布尔值、常见整数和浮点数；单条记录的 `Buffer` 按需增长，超过原先 4000 字节的正文也会完整保留。一条日志在完整表达式结束时统一输出。
- 输出方式：默认写入 `stdout`，也可通过 `dlog::Logger::SetOutput()` 设置自定义回调。回调收到日志数据指针和有效字节长度；可选择输出 ANSI 等级颜色，写文件时可关闭颜色码。
- 文件写入与轮转：`dlog::LogFile(filename)` 追加到固定文件；`dlog::LogFile(basePath, rollSize)` 按本地日期和文件大小轮转，文件名形如 `app.20260924.0.log`。程序重启后会检查当天已有的序号，继续使用新的序号，避免覆盖旧日志。
- 文件输出接口：`dlog::FileFlush(filename)` 写普通文件，`dlog::RollFileFlush(basePath, rollSize)` 写轮转文件；轮转逻辑仍由 LogFile 管理。`writeAndFlush(data, len)` 一次完成写入和刷新，零长度直接返回，缺失的父目录会自动创建。
- 异步写盘：`dlog::AsyncLogging` 将前台收到的日志先放入内存缓冲区，再由后台通过 `FileFlush` 按缓冲块写入并刷新文件。停止运行中的后台线程时，`stop()` 会等待它处理完剩余数据。
- 系统错误日志：`DLOG_SYS_ERROR(savedErrno)` 和 `DLOG_SYS_FATAL(savedErrno)` 接收调用方预先保存的错误码，并在消息前附加错误描述和数值，便于定位系统调用失败原因。

## 基本用法

直接使用 `LogStream::buffer()` 时，返回类型为 `const dlog::Buffer&`；通过 `peek()` 和 `readableBytes()` 读取内容。原 `SmallBuffer` 的返回类型和读取接口已替换，使用方需要同步调整并重新编译。

直接使用 `LogFile` 时，`append()` 不再按时间主动刷新，应显式调用 `flush()` 或使用 `FileFlush::writeAndFlush()`。`LogFile` 和 `FileFlush` 不再接受刷新间隔参数；周期等待仍由 `AsyncLogging` 的 `flushInterval` 控制。

`FileUtil`、`LogFile`、`FileFlush` 和 `RollFileFlush` 构造函数接收 `const std::filesystem::path&`，已有字符串调用仍可转换使用；Windows 下使用原生宽字符路径打开文件。直接传入路径对象可避免先调用 `.string()` 丢失字符。`AsyncLogging` 的字符串路径接口及示例留待后续迁移；当前仍使用 FileFlush 的双参数入口，待改用 RollFileFlush 后，该入口将改为 protected。

相对文件路径统一以 `FileUtil::executableDirectory()` 为基准，并做 `lexically_normal()` 规范化，不再依赖当前工作目录；绝对路径直接使用。`FileUtil::executableFullPath()` 返回进程可执行文件的绝对路径，获取失败抛出 `std::system_error`（包括派生异常）。缺失的父目录会自动创建，轮转文件的扫描与写入使用同一绝对基准；父目录路径被普通文件占用或创建失败时抛出 std::filesystem::filesystem_error。

```cpp
#include <dlog/core/logger.h>

int main()
{
    DLOG_INFO() << "server started, port=" << 8080;
    DLOG_WARN() << "retry=" << 2 << ", enabled=" << true;
}
```

输出示例：

```text
[2026/09/24 15:30:12.123456] [INFO ] [main.cpp:6] server started, port=8080
```

日志宏在完整表达式结束时输出。FATAL 日志会调用刷新回调，然后终止进程。

系统错误日志应在系统调用失败后立即保存 `errno`，再传给日志宏：

```cpp
#include <cerrno>
#include <dlog/core/logger.h>

// if (open(...) < 0) {
const int savedErrno = errno;
DLOG_SYS_ERROR(savedErrno) << "open file failed";
// }
```

## 自定义输出

输出回调接收数据指针和字节长度。应在开始写日志前设置回调；日志文件通常不需要终端颜色：

```cpp
#include <cstdio>
#include <dlog/core/logger.h>

dlog::Logger::SetOutput(
    [](const char *data, size_t length) {
        std::fwrite(data, 1, length, stdout);
    },
    dlog::LogLevelColorMode::OFF);
```

## 异步文件输出

`dlog::AsyncLogging` 可通过输出回调接收 `dlog::Logger` 格式化后的日志。日志目录需预先创建；停止时应先停止产生日志的线程，再切回其他输出，最后停止异步后端：

```cpp
#include <cstdio>
#include <filesystem>
#include <dlog/core/asynclogging.h>
#include <dlog/core/logger.h>

#include <dlog/utils/fileutil.h>

std::filesystem::create_directories(dlog::FileUtil::executableDirectory() / "logs");
dlog::AsyncLogging logging("logs/app", 64LL * 1024 * 1024);
logging.start();

dlog::Logger::SetOutput(
    [&logging](const char *data, size_t length) {
        logging.append(data, length);
    },
    dlog::LogLevelColorMode::OFF);

DLOG_INFO() << "written by the background thread";

// 停止所有日志生产者后执行：
dlog::Logger::SetOutput([](const char *data, size_t length) {
    std::fwrite(data, 1, length, stdout);
});
logging.stop();
```

日志文件名形如 `logs/app.YYYYMMDD.0.log`。更多示例见 [`snippet/AsyncLoggingTest/main.cpp`](snippet/AsyncLoggingTest/main.cpp)。

