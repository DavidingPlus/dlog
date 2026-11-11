# dlog

dlog 是一个基于 C++17 的轻量级日志库，支持异步输出和文件轮转。公共 C++ API 位于 `dlog` 命名空间中；`DLOG_*` 日志宏保留为全局宏。

## 功能

- 日志等级：提供 TRACE、DEBUG、INFO、WARN、ERROR 和 FATAL 六个等级。FATAL 表示最高严重级别，不会自动刷新或终止进程。
- 日志格式：每条记录包含本地时间（精确到微秒）、源文件名、代码行号和消息正文，格式为 `[时间] [等级] [文件名:行号] 消息`。源文件名从编译器提供的文件路径中提取。
- 消息拼接：日志宏采用流式写法，例如 `DLOG_INFO() << "request=" << requestId`。支持字符串、字符、布尔值、常见整数和浮点数；单条记录的 `FlexibleBuffer` 按需增长，超过原先 4000 字节的正文也会完整保留。一条日志在完整表达式结束时统一输出。
- 记录实现：长期 `Logger` 支持单目标异步输出，宏和对象接口共用 `LogMessage` 管理单条记录。
- 输出方式：默认日志器写入 `stdout`；可创建文件、轮转文件或自定义 `LogFlush` 输出目标。
- 文件写入与轮转：`dlog::LogFile(filename)` 追加到固定文件；`dlog::LogFile(basePath, rollSize)` 按本地日期和文件大小轮转，文件名形如 `app.20260924.0.log`。程序重启后会检查当天已有的序号，继续使用新的序号，避免覆盖旧日志。
- 文件输出接口：`dlog::FileFlush(filename)` 写普通文件，`dlog::RollFileFlush(basePath, rollSize)` 写轮转文件；轮转逻辑仍由 LogFile 管理。`writeAndFlush(data, len)` 一次完成写入和刷新，零长度直接返回，缺失的父目录会自动创建。
- 异步写盘：`Logger` 通过 `LogWorker` 缓冲并在后台批量写入；正常 `stop()` 等待剩余数据处理完成。
- 系统错误日志：`DLOG_SYS_ERROR(savedErrno)` 和 `DLOG_SYS_FATAL(savedErrno)` 接收调用方预先保存的错误码，并在消息前附加错误描述和数值，便于定位系统调用失败原因。

## 基本用法

`FlexibleBuffer` 和 `FixedBuffer` 位于 utils 模块，公共头文件为 `<dlog/utils/flexiblebuffer.h>`、`<dlog/utils/fixedbuffer.h>`。直接使用 `LogStream::buffer()` 时，返回类型为 `const dlog::FlexibleBuffer&`；通过 `readableBegin()` 和 `readableBytes()` 读取内容。

可增长类型原名 `Buffer`，现为 `FlexibleBuffer`，源文件及测试文件也使用 flexiblebuffer 命名。固定容量预设为 `kSmallFixedBufferSize`（4000 字节）和 `kLargeFixedBufferSize`（4000000 字节），对应类型别名为 `SmallFixedBuffer` 和 `LargeFixedBuffer`。使用方需同步更新旧类型名、常量名和头文件路径，并重新编译。

两者统一提供 `storageData()`、`capacity()`、`readableBegin()`、`readableBytes()`、`writableBegin()`、`writableBytes()`、`append()`、`clear()` 和 `toString()`。`storageData()` 指向整块存储，`readableBegin()` 指向尚未消费的数据，`writableBegin()` 指向尾部可写区域；`capacity()` 是全部存储的字节数，`writableBytes()` 只计算尾部连续空间。`clear()` 清空逻辑数据但不清零或缩容，`toString()` 复制可读数据而不消费。FlexibleBuffer 的消费接口为 `consume()`、`consumeAll()`、`consumeAsString()`、`consumeAllAsString()`；`consumeAll()` 对应原来的 `retrieveAll()`，内部复用 `clear()`，保留消费全部数据的独立语义，保留 `ensureWritableBytes()`；FixedBuffer 的物理清零接口为 `bzero()`。

`append(nullptr, 0)` 是空操作，正长度空指针抛出 `std::invalid_argument`。FlexibleBuffer 按需增长；FixedBuffer 超限追加抛出 `std::length_error`，保留原内容。旧的 `peek/beginWrite/length/avail/current/reset/retrieve*` 接口已替换；FixedBuffer 原 `data()` 调用读取内容时改用 `readableBegin()`，借用整块存储时改用 `storageData()`。头文件路径及 FileUtil 对象布局也已改变，使用方需迁移并重新编译。

FileUtil 使用 `FixedBuffer<64 * 1024>` 为文件流提供固定存储，通过 `storageData()` 和 `capacity()` 传给 `setvbuf()`。借出后不再调用该对象的追加、清空或清零接口，存储在 `fclose()` 完成前始终有效；文件流的缓冲状态由 C 运行库维护，不能通过 FixedBuffer 的 `readableBytes()` 查询。

直接使用 `LogFile` 时，`append()` 不按时间主动刷新，应显式调用 `flush()` 或使用 `FileFlush::writeAndFlush()`。

`FileUtil`、`LogFile`、`FileFlush` 和 `RollFileFlush` 的路径参数接收 `const std::filesystem::path&`；Windows 下使用原生宽字符路径打开文件。直接传入路径对象可避免先调用 `.string()` 丢失字符。轮转输出使用 `RollFileFlush(basePath, rollSize)`。

相对文件路径统一以 `FileUtil::executableDirectory()` 为基准，并做 `lexically_normal()` 规范化，不再依赖当前工作目录；绝对路径直接使用。`FileUtil::executableFullPath()` 返回进程可执行文件的绝对路径，获取失败抛出 `std::system_error`（包括派生异常）。缺失的父目录会自动创建，轮转文件的扫描与写入使用同一绝对基准；父目录路径被普通文件占用或创建失败时抛出 std::filesystem::filesystem_error。

```cpp
#include <dlog/core/log.h>

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

日志宏在完整表达式结束时提交；未满块的数据由后台周期处理，默认间隔为 3 秒，线程调度或慢输出可能导致更长延迟，不保证立即显示。FATAL 日志不会终止进程。

系统错误日志应在系统调用失败后立即保存 `errno`，再传给日志宏：

```cpp
#include <cerrno>
#include <dlog/core/log.h>

// if (open(...) < 0) {
const int savedErrno = errno;
DLOG_SYS_ERROR(savedErrno) << "open file failed";
// }
```

## 自定义输出

通过继承 `LogFlush` 实现 `writeAndFlush()`，再创建显式日志器。回调在后台线程运行，数据指针仅在本次调用期间有效：

```cpp
#include <dlog/core/log.h>
#include <cstdio>

class MyOutput : public dlog::LogFlush {
public:
    void writeAndFlush(const char *data, size_t length) override {
        std::fwrite(data, 1, length, stdout);
        std::fflush(stdout);
    }
};

auto logger = dlog::Logger::BuildLogger<MyOutput>();
DLOG_LOGGER_INFO(logger) << "custom output";
logger->stop();
```

记录析构中的补换行或提交发生异常时，只向 stderr 报告，不让异常逃出析构。

## Logger 单目标输出

`Logger` 是长期持有的异步日志器，每个实例使用一个输出目标。`LogManager` 提供固定的默认终端日志器，并管理显式注册的命名日志器：

```cpp
#include <dlog/core/log.h>
#include <filesystem>

int main()
{
    auto &manager = dlog::LogManager::GetInstance();
    auto file = dlog::Logger::BuildLogger<dlog::FileFlush>(std::filesystem::path("logs") / "app.log");
    manager.registerLogger("file", file);

    DLOG_INFO() << "console message";
    DLOG_LOGGER_INFO(manager.getLogger("file")) << "file message";
    // 程序正常退出时由 Manager 停止并排空已注册日志器。
}
```

普通文件使用 `BuildLogger<FileFlush>(filename)`，轮转文件使用 `BuildLogger<RollFileFlush>(basePath, rollSize)`，终端可使用 `BuildLogger()` 或 Manager 的默认日志器。每个调用创建独立的单目标实例；终端默认启用等级颜色，文件默认使用纯文本。相对文件路径以可执行文件所在目录为基准。完整示例见 [`snippet/DLogTest/main2.cpp`](snippet/DLogTest/main2.cpp)。仓库中的示例按需构建：在 `snippet/DLogTest` 目录创建空的 `.buildme` 文件后，运行 `xmake -y DLogTest2` 和 `xmake run DLogTest2`。

默认宏始终写入终端，显式宏 `DLOG_LOGGER_TRACE/DEBUG/INFO/WARN/ERROR/FATAL(logger)` 和 `DLOG_LOGGER_SYS_ERROR/SYS_FATAL(logger, savedErrno)` 写入指定日志器；空日志器抛出 `std::invalid_argument`。也可以使用 `message(__FILE__, __LINE__, level).stream()`。FATAL 与其他等级一样异步提交，不会立即排空或终止进程。

首次使用默认宏时会初始化 Manager 和默认日志器；初始化失败会向调用方抛出异常。

停止或自动回收前，先结束生产线程及临时消息。未注册的日志器在最后一个持有者释放时自动回收；也可主动调用 `stop()` 等待排空，并捕获后台输出异常。前台提交失败只输出 stderr 诊断，`stop()` 无法反馈该错误。后台写入失败后放弃剩余待写数据，不自动重试。Manager 析构会停止默认和命名日志器；若停止失败，仍尝试关闭其他日志器，最后触发进程终止。正常退出的排空已在 Windows EXE + DLL 场景验证；跨 DLL 不共享 Manager，也不保证其他 DLL 的退出排空。

## 异步文件输出

旧的临时记录类型 `Logger`、`SetOutput`、`SetFlush` 和 `AsyncLogging` 已移除；现在的 `Logger` 是长期持有的异步日志器。此前的 `AsyncLogger`、`AsyncWorker` 和 `BuildAsyncLogger()` 分别改名为 `Logger`、`LogWorker` 和 `BuildLogger()`，头文件改为 `logger.h` 和 `logworker.h`；使用方需迁移并重新编译。后台会自动创建缺失的父目录：

```cpp
#include <filesystem>
#include <dlog/core/log.h>

const auto basePath = std::filesystem::path("logs") / "app";
auto logger = dlog::Logger::BuildLogger<dlog::RollFileFlush>(basePath, 64LL * 1024 * 1024);
DLOG_LOGGER_INFO(logger) << "written by the background thread";

// 停止所有日志生产者后执行：
logger->stop();
```

日志文件名形如 `logs/app.YYYYMMDD.0.log`。更多示例见 [`snippet/DLogTest/main2.cpp`](snippet/DLogTest/main2.cpp)。

## 独立 Worker

Worker 不格式化日志、不保存文件配置，输出方式由回调决定。构造不启动线程；可以先追加数据，再启动和停止：

```cpp
#include <dlog/core/logworker.h>
#include <string>

std::string received;
dlog::LogWorker worker([&received](const dlog::LargeFixedBuffer &buffer) {
    received.append(buffer.readableBegin(), buffer.readableBytes());
});
worker.append("hello\n", 6);
worker.start();
worker.stop();
// stop/join 完成后读取 received，内容为 "hello\n"。
```

回调在后台、队列锁外执行，接收 `const LargeFixedBuffer &`，通过 `readableBegin()` 和 `readableBytes()` 读取本批字节。缓冲区的清空和复用由 Worker 管理，引用及数据指针仅在本次回调期间有效；回调及捕获对象需活到 stop/join 完成后，不得从回调中启停或销毁 Worker。多个生产者可并发追加，启停操作由调用方串行执行。停止前先结束生产者；正常停止后允许缓存新数据并再次启动。默认按整块唤醒、未满块周期处理，间隔为秒，传入 0 时至少等待 1 秒。

后台抛出异常时，Worker 停止消费并放弃剩余待写数据。显式 `stop()` 等线程退出后重抛原异常，析构只负责回收。失败后的非空 `append()` 和 `start()` 也会重抛该异常。未启动时 `stop()` 不输出缓存数据。

