# dlog

dlog 是一个基于 C++17 的轻量级日志库，支持同步输出、文件轮转和异步写盘。

## 功能

- 日志等级：提供 TRACE、DEBUG、INFO、WARN、ERROR 和 FATAL 六个等级。等级会显示在每条记录中；FATAL 记录输出后调用刷新回调并终止进程。
- 日志格式：每条记录包含本地时间（精确到微秒）、源文件名、代码行号和消息正文，格式为 `[时间] [等级] [文件名:行号] 消息`。源文件名从编译器提供的文件路径中提取。
- 消息拼接：日志宏采用流式写法，例如 `DLOG_INFO() << "request=" << requestId`。支持字符串、字符、布尔值、常见整数和浮点数；一条日志在完整表达式结束时统一输出。
- 输出方式：默认写入 `stdout`，也可通过 `Logger::SetOutput()` 设置自定义回调。回调收到日志数据指针和有效字节长度；可选择输出 ANSI 等级颜色，写文件时可关闭颜色码。
- 文件写入与轮转：`FileUtil` 为文件写入提供缓冲；`LogFile` 按本地日期和文件大小切换文件，文件名形如 `app.20260924.0.log`。程序重启后会检查当天已有的序号，继续使用新的序号，避免覆盖旧日志。
- 异步写盘：`AsyncLogging` 将前台收到的日志先放入内存缓冲区，再由后台线程批量写入 `LogFile`，减少业务线程直接执行文件写入的开销。停止运行中的后台线程时，`stop()` 会等待它处理完剩余数据并刷新文件。
- 系统错误日志：`DLOG_SYS_ERROR(savedErrno)` 和 `DLOG_SYS_FATAL(savedErrno)` 接收调用方预先保存的错误码，并在消息前附加错误描述和数值，便于定位系统调用失败原因。

## 基本用法

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

Logger::SetOutput(
    [](const char *data, size_t length) {
        std::fwrite(data, 1, length, stdout);
    },
    LogLevelColorMode::OFF);
```

## 异步文件输出

`AsyncLogging` 可通过输出回调接收 Logger 格式化后的日志。日志目录需预先创建；停止时应先停止产生日志的线程，再切回其他输出，最后停止异步后端：

```cpp
#include <cstdio>
#include <filesystem>
#include <dlog/core/asynclogging.h>
#include <dlog/core/logger.h>

std::filesystem::create_directories("logs");
AsyncLogging logging("logs/app", 64LL * 1024 * 1024);
logging.start();

Logger::SetOutput(
    [&logging](const char *data, size_t length) {
        logging.append(data, length);
    },
    LogLevelColorMode::OFF);

DLOG_INFO() << "written by the background thread";

// 停止所有日志生产者后执行：
Logger::SetOutput([](const char *data, size_t length) {
    std::fwrite(data, 1, length, stdout);
});
logging.stop();
```

日志文件名形如 `logs/app.YYYYMMDD.0.log`。更多示例见 [`snippet/AsyncLoggingTest/main.cpp`](snippet/AsyncLoggingTest/main.cpp)。

