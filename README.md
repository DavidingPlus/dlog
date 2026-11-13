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

## 构建、安装和外部使用

项目使用 XMake、C++17 编译器和 `fmt`；运行单元测试还需要 GoogleTest。XMake 根据项目配置解析这些依赖。

```sh
xmake f -m debug --with_gtest=y
xmake -y dlog
xmake test -y
xmake install -o <install-dir> -y dlog
```

Windows 下使用 `--build_shared=y` 构建 DLL，使用 `--build_shared=n` 构建静态库；Linux 同样支持该选项。未指定 `-o` 时，默认安装到构建目录下对应平台、架构和模式的 `install` 目录。安装会重建 `<install-dir>/include/dlog`，并覆盖本项目的同名库文件；不要将需要保留的其他文件放在 `include/dlog` 下。

外部项目需要将 `<install-dir>/include` 加入头文件搜索路径，并链接 `<install-dir>/lib` 中的 dlog 和对应的 `fmt` 依赖。Windows DLL 消费方编译时还需定义 `DLOG_BUILD_SHARED`，运行时需让系统找到 `<install-dir>/bin/dlog.dll`；静态库消费方不定义该宏。Linux 动态库消费方需让运行时找到安装目录下的 `libdlog.so`，例如配置运行时库搜索路径。

此前版本的安装验证覆盖 Windows / MSVC / Debug 的 DLL 与静态库，以及 Ubuntu WSL / x86-64 / Debug 动态库：外部程序仅使用安装后的 dlog 头文件、库和独立配置的 `fmt`，验证了默认终端、普通文件、轮转文件、显式停止和注册日志器正常退出排空。Windows DLL 安装的 18 个公开头文件分别通过独立包含编译。该验证不代表所有公开符号及平台组合均已验证；Linux 静态库、Release 模式、真实磁盘故障及长期压力仍未验证。

## 性能基准测试

基准程序使用公开日志宏和异步 Logger，分别测试计数型自定义输出和普通文件输出，各运行一次单生产者和指定数量的多生产者。它不参与默认构建或单元测试，也不设置性能通过阈值。建议在 Release 模式运行：

```sh
xmake f -m release
xmake build -y dlog-bench
xmake run dlog-bench 100000 4
```

两个参数分别是每个场景的总记录数和多生产者线程数，均为正整数；省略时分别为 100000 和 4。每个场景报告提交耗时、包含 `stop()` 排空的总耗时及对应吞吐量。程序在计时结束后检查输出的换行记录数，并报告字节数；这不是逐条内容、顺序或重复记录的完整性验证。文件场景在可执行文件所在目录下新建独立子目录，路径在运行时打印；全部场景成功后自动清理本次文件和目录，测试失败时保留供排查。清理失败时会报告目录，已删除的文件不会恢复。

计数输出不写磁盘，但仍包含日志格式化和计数开销；文件输出每批写入后刷新。对比结果时应记录平台、编译模式、线程数和存储设备，并重复运行。不同日志库的格式、刷新和队列满策略不同时，结果不能直接比较。

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

日志宏在完整表达式结束时提交；有数据时安排后台处理，线程池调度或慢输出可能导致延迟，不保证立即显示。FATAL 日志不会终止进程。

正常退出时 Manager 会自动清理。需要主动控制清理时机（例如 Windows 业务 DLL 卸载前），可在停止外部日志生产者、线程池任务提交者并结束临时日志消息后调用：

```cpp
dlog::LogManager::Shutdown();
```

调用会先等待共享线程池空闲，再停止并排空 Manager 管理的日志器，最后关闭线程池。等待期间，池中任务仍可提交后续任务和日志。关闭后 Manager 不可重新使用。请在普通控制线程中串行调用，不要在 `DllMain`、线程池任务或日志输出回调中调用。详见 [Windows DLL 退出说明](docs/windows-dll-exit.md)。

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

auto logger = dlog::LogManager::GetInstance().buildLogger<MyOutput>("custom");
DLOG_LOGGER_INFO(logger) << "custom output";
logger->stop();
```

记录析构中的补换行或提交发生异常时，只向 stderr 报告，不让异常逃出析构。

## Logger 单目标输出

`Logger` 是长期持有的异步日志器，每个实例使用一个输出目标。`LogManager` 提供固定的默认终端日志器，并创建、持有命名日志器：

```cpp
#include <dlog/core/log.h>
#include <filesystem>

int main()
{
    auto &manager = dlog::LogManager::GetInstance();
    auto file = manager.buildLogger<dlog::FileFlush>("file", std::filesystem::path("logs") / "app.log");

    DLOG_INFO() << "console message";
    DLOG_LOGGER_INFO(manager.getLogger("file")) << "file message";
    // 程序正常退出时由 Manager 停止并排空已注册日志器。
}
```

普通文件使用 `manager.buildLogger<FileFlush>(name, filename)`，轮转文件使用 `manager.buildLogger<RollFileFlush>(name, basePath, rollSize)`，终端可使用 `manager.buildLogger(name)` 或 Manager 的默认日志器。创建时自动注册，空名称或重复名称会抛出异常。并发创建同名日志器时，落败的调用可能已构造输出目标，再因重名失败。每个调用创建单目标实例；终端默认启用等级颜色，文件默认使用纯文本。相对文件路径以可执行文件所在目录为基准。完整示例见 [`snippet/DLogTest/main2.cpp`](snippet/DLogTest/main2.cpp)。仓库中的示例按需构建：在 `snippet/DLogTest` 目录创建空的 `.buildme` 文件后，运行 `xmake -y DLogTest2` 和 `xmake run DLogTest2`。

默认宏始终写入终端，显式宏 `DLOG_LOGGER_TRACE/DEBUG/INFO/WARN/ERROR/FATAL(logger)` 和 `DLOG_LOGGER_SYS_ERROR/SYS_FATAL(logger, savedErrno)` 写入指定日志器；空日志器抛出 `std::invalid_argument`。也可以使用 `message(__FILE__, __LINE__, level).stream()`。FATAL 与其他等级一样异步提交，不会立即排空或终止进程。

首次使用默认宏时会初始化 Manager 和默认日志器；初始化失败会向调用方抛出异常。

停止或自动回收前，先结束生产线程及临时消息。Manager 一直持有创建的日志器及其输出目标，直到 Manager 析构；自定义输出引用的外部数据必须保持有效。也可主动调用 `stop()` 等待排空，并捕获后台输出异常。前台提交失败只输出 stderr 诊断，`stop()` 无法反馈该错误。后台写入失败后放弃剩余待写数据，不自动重试。Manager 析构会停止默认和命名日志器；若停止失败，仍尝试关闭其他日志器，最后触发进程终止。正常退出的排空已在 Windows EXE + DLL 场景验证；跨 DLL 不共享 Manager，也不保证其他 DLL 的退出排空。

## 异步文件输出

旧的临时记录类型 `Logger`、`SetOutput`、`SetFlush` 和 `AsyncLogging` 已移除；现在的 `Logger` 是长期持有的异步日志器。此前的 `AsyncLogger`、`AsyncWorker` 和 `BuildAsyncLogger()` 分别改名为 `Logger`、`LogWorker` 和 `buildLogger()`，头文件改为 `logger.h` 和 `logworker.h`；创建入口由 `Logger::BuildLogger()` 迁至 `LogManager::GetInstance().buildLogger(name, ...)`，创建后自动注册，使用方需迁移并重新编译。后台会自动创建缺失的父目录：

```cpp
#include <filesystem>
#include <dlog/core/log.h>

const auto basePath = std::filesystem::path("logs") / "app";
auto logger = dlog::LogManager::GetInstance().buildLogger<dlog::RollFileFlush>("rolling", basePath, 64LL * 1024 * 1024);
DLOG_LOGGER_INFO(logger) << "written by the background thread";

// 停止所有日志生产者后执行：
logger->stop();
```

日志文件名形如 `logs/app.YYYYMMDD.0.log`。更多示例见 [`snippet/DLogTest/main2.cpp`](snippet/DLogTest/main2.cpp)。

## 独立 Worker

Worker 不格式化日志、不保存文件配置，输出方式由回调决定。构造不启动线程；可以先追加数据，再启动和停止：

```cpp
#include <dlog/core/logworker.h>
#include <dlog/utils/threadpool.h>
#include <memory>
#include <string>

auto pool = std::make_shared<dlog::ThreadPool>(2);
std::string received;
dlog::LogWorker worker([&received](const dlog::FlexibleBuffer &buffer) {
    received.append(buffer.readableBegin(), buffer.readableBytes());
}, pool);
worker.append("hello\n", 6);
worker.start();
worker.stop();
// stop 完成后读取 received，内容为 "hello\n"。
```

回调在后台、队列锁外执行，接收 `const FlexibleBuffer &`，通过 `readableBegin()` 和 `readableBytes()` 读取本批字节。缓冲区的清空和复用由 Worker 管理，引用及数据指针仅在本次回调期间有效；回调及捕获对象需活到 stop 完成后，不得从回调中启停或销毁 Worker。多个生产者可并发追加，启停操作由调用方串行执行。停止前先结束生产者；正常停止后允许缓存新数据并再次启动。有数据时安排线程池任务，输出期间可继续追加；线程池由调用方提供并管理。

后台抛出异常时，Worker 停止消费并放弃剩余待写数据。显式 `stop()` 等任务结束后重抛原异常，析构只负责回收。失败后的非空 `append()` 和 `start()` 也会重抛该异常。未启动时 `stop()` 不输出缓存数据。

