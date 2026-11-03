# dlog 与 Kama 对比及简化迁移计划

## 1. 目标与进度

更新日期：2026-11-03；当前代码基线：`b2d0bf0`。本文合并原对比文档与迁移计划，后续只维护这一份。

采用 Kama 的核心用法：**长期存在的 AsyncLogger 管理工作器和输出目标，使用者创建日志器后直接写日志。** 保留 dlog 的流式宏，先完成终端和文件输出，不扩展成通用日志框架。

- [x] 动态 Buffer 接入 LogStream 和 Logger，超长记录完整交付。
- [x] 验证 5000 字节正文、空字节、颜色、边界换行及超过两个异步缓冲块的记录完整落盘。
- [ ] 输出目标与异步工作器解耦。
- [ ] AsyncLogger、默认入口、自动启动关闭及 FATAL 排空。
- [ ] 简单命名管理和多个输出目标。

上次实现验证为 Windows 全量 172 项测试通过、相关示例编译运行正常；不代表下述待实施功能已经通过验收。本次仅整理文档，接口示例均是待实现方案。

## 2. 当前 dlog 与 Kama 的联系和区别

两边都是业务线程格式化并复制日志，后台线程批量输出；格式化、分配、复制和锁竞争仍发生在业务线程。异步不等于无锁，也不等于业务线程完全不等待。

| 方面 | 当前 dlog | Kama 参考实现 | 本轮选择 |
| --- | --- | --- | --- |
| 日志入口 | `DLOG_INFO() << value` | `Info(fmt, ...)` 及默认宏 | 保留流式宏与文件名、行号捕获 |
| 日志器含义 | 临时 Logger 构造一条记录 | 长期 AsyncLogger 接收多条记录 | 保留临时 Logger，新增长期 AsyncLogger，不先改名 |
| 异步接入 | 调用方创建 AsyncLogging 并设置全局回调 | AsyncLogger 内部创建工作器 | 库内部完成连接与生命周期管理 |
| 默认输出 | 同步 stdout | 默认日志器异步输出终端 | 迁为默认异步终端，属于可见的行为变化 |
| 输出目标 | 回调连接文件等后端 | LogFlush 接口及输出目标列表 | 先单目标，再支持列表 |
| 日志器管理 | 进程共享回调 | LoggerManager 保存默认和命名日志器 | 使用简单管理器 |
| 缓冲 | 单条记录用动态 Buffer，后台用 LargeBuffer 块队列 | 两个 vector 缓冲区交换；SAFE 等待、UNSAFE 扩容 | 保留 dlog 已验证的缓冲组织，不直接替换 |
| 唤醒 | 整块入队通知，未满数据周期处理，默认 3 秒 | 每次 Push 通知消费者 | 核心迁移先保留；终端低流量延迟需观察再调整 |
| 文件与轮转 | LogFile 按日期、大小轮转，启动扫描序号；FileUtil 补写部分写入 | FileFlush / RollFileFlush，按批次检查大小 | 继续复用 dlog 的文件层 |
| FATAL | 输出、刷新回调、abort；异步回调未自动接通 | 等待备份后提交异步输出，不主动终止进程 | 保留 dlog 的终止语义，退出前排空目标 |
| 配置及平台 | 构造参数；Windows / Linux 适配 | JSON、Builder、全局指针；偏向 POSIX | 本轮不迁配置、Builder、网络备份或线程池 |

原先 4000 字节 SmallBuffer 导致正文整段丢失的问题已经修复；后端轮转只能处理已收到的数据，无法恢复前端曾经丢弃的字节。不要继续把旧问题列为未完成项。

### 参考实现值得借鉴，但不能原样复制的部分

以下是参考源码的静态分析结论，未运行 Kama 验证：

| 问题 | 阅读时注意什么 |
| --- | --- |
| 停止时可能漏尾部，重复 Stop 无条件 join | 保留 dlog 的“停止请求到达且待处理数据为空才退出”和重复停止安全规则 |
| SAFE 超大追加可能一直等；UNSAFE 单次扩容可能不够 | 不能直接移植容量等待；扩容后必须足够，计算不得溢出 |
| 文件打开、部分写入和资源关闭处理不完整 | 保留 dlog 的文件所有权及补写逻辑，并完善失败反馈 |
| ERROR / FATAL 提交备份任务后立即 future.get | 网络等待仍会阻塞调用方；本轮不迁远程备份 |

Kama 的 SAFE 表示容量等待策略，不代表整个实现已经可靠。相似结构也不代表直接移植关系；dlog 原 LogStream 注释引用的是同作者的另一个 kamaLog 仓库。

## 3. 迁移后怎样使用

默认异步输出终端：

```cpp
DLOG_INFO() << "request=" << 42;

// 所有业务线程退出后：
dlog::Shutdown();
```

切换为异步文件日志器：

```cpp
auto app = dlog::CreateFileLogger("app", "logs/app", 64LL * 1024 * 1024);
dlog::SetDefaultLogger(app);

DLOG_INFO() << "request=" << 42;

// 所有业务线程退出后：
dlog::Shutdown();
```

`"app"` 是日志器名称，`"logs/app"` 是基础路径；第一版复用现有轮转，实际文件类似 `logs/app.YYYYMMDD.0.log`。创建函数准备目录、文件和工作器，失败在初始化处报告。调用方不再手动接 SetOutput、SetFlush 或调用后端 start。

默认终端从同步改为异步后，调用返回不代表终端已经显示内容；正常退出需要排空。

## 4. 组件分工

| 组件 | 职责与迁移方式 |
| --- | --- |
| 现有 Logger / LogStream / Buffer | 构造单条记录，完成后提交；保留格式、空字节和超长支持 |
| AsyncLogger | 长期持有工作器与输出目标，接收完整记录 |
| AsyncWorker | 从 AsyncLogging 提取，负责字节复制、后台写出、周期刷新、停止排空 |
| LogFlush | 单一接口 `writeAndFlush(data, len)`：按显式长度写入并刷新；长度为 0 时直接返回 |
| StdoutFlush / FileFlush | 分别复用 stdout 与 LogFile；此处 FileFlush 指轮转文件输出 |
| 简单管理器 | 保存默认和命名 AsyncLogger，统一关闭 |

```text
DLOG_INFO()
  → 临时 Logger + LogStream + Buffer
  → 长期 AsyncLogger
  → AsyncWorker
  → StdoutFlush 或 FileFlush
```

每个 AsyncLogger 一个工作器，先不共享线程池。现有 Buffer、LargeBuffer、跨块追加和文件轮转继续复用；FixedBuffer 不承担日志截断、分片策略或警告输出。

## 5. 分三步实施

### 第一步：输出目标与工作器解耦

- 定义 LogFlush 的 `writeAndFlush(data, len)` 接口，一次完成写入与刷新，实现终端和轮转文件输出。后台按批调用，避免每条日志都单独刷新。
- 从 AsyncLogging 提取工作器，把写死的 LogFile 改为调用输出目标，只保留一套消费循环。原 AsyncLogging 调用在迁移期间继续通过该循环工作，不另写一个后端。
- 保留持锁整条追加、跨块顺序、锁外写出、周期刷新、重复启停和停止排空。
- 后台捕获失败、保存错误状态并直接向 stderr 报告，避免异常逃出线程或递归记录。FileUtil 已有的报错返回路径也要检查，单加 catch 不够。

验收：后台可输出到终端或文件；长记录、空字节、跨块、重启与排空测试通过；失败可识别，停止不会永久等待。

涉及：`src/core/asynclogging.*`、新增输出接口和工作器、必要的 `logfile.*` / `fileutil.*` 调整及对应测试。

### 第二步：增加 AsyncLogger，打通默认使用

- AsyncLogger 负责创建资源、启动工作器和关闭排空；默认首次使用时创建终端日志器，提供文件创建函数、SetDefaultLogger 和 Shutdown。
- 临时 Logger 构造时取得目标 AsyncLogger 强引用，析构时提交一次；异步接收立即复制字节，后台不保存临时 Buffer 指针。
- 库内部连接输出和刷新，示例使用第三节的写法；旧静态回调只保留已有必要用途，不扩展兼容框架。
- FATAL 提交后停止并排空目标，再 abort。后台失败应报告后再终止；暂不增加可重复调用的异步 flush 屏障。
- Shutdown 排空本轮创建的日志器，重复关闭安全；旧对象关闭后拒绝追加并报告。工作器先退出，再释放输出目标，不能 join 自身或形成 shared_ptr 循环。

验收：使用者不接回调也能异步写终端和文件；正常关闭尾部完整；FATAL 子进程退出后文件包含致命日志及此前记录；初始化和写入失败有反馈。

涉及：`src/core/logger.*`、新增 AsyncLogger 和默认入口、README、异步示例及子进程集成测试。

### 第三步：简单命名管理与多个输出目标

- 小型管理器保存默认和命名 AsyncLogger，将第二步已有集合收口，不维护两套对象列表。
- 增加按名称查询和显式流式宏，例如 `DLOG_LOGGER_INFO(app) << ...`；宏只求值一次目标表达式，仍在调用处捕获文件位置。
- 重复名称拒绝，查不到返回空指针；显式入口检查空目标。配置和默认切换在业务线程启动前完成。
- 输出目标扩为列表，同一工作器依次输出；一个目标失败仍尝试其他目标并报告。慢目标会拖慢该日志器。
- 混合终端与文件先统一纯文本，单终端保留颜色；独立格式和每目标等级以后按需增加。
- 更新 README、snippet，检查动态库导出和安装后的公共头文件使用。

验收：不同日志器内容独立；一条记录恰好交给各目标；文件没有 ANSI；默认宏保持好用；关闭后没有悬空回调。

## 6. 本轮边界与后续事项

- 关闭前先结束日志生产者及临时记录；显式 Shutdown 是正常退出路径，析构兜底。第一版不承诺运行中并发切换配置或全局关闭后重新初始化。
- 完成接收、完成 fflush、完成系统存储同步是不同阶段。本轮只保证文件层刷新，不承诺断电持久性，也不直接迁 Kama 的 fsync 配置。
- 待写队列仍无总量上限。核心用法完成后再单独定义积压上限和满载策略，用可控慢输出测试超大消息、并发、停止和失败，不照搬可能永久等待的 SAFE 模式。
- 现有 LogFile 允许空文件接收超限批次；跨块记录可能跨文件，不承诺文件大小硬上限或按行轮转。
- 性能优化等测量后再做；比较前对齐消息量、线程数和刷新语义，同时核对实际字节、记录数、峰值内存与停止耗时。
- 同步框架、共享线程池、复杂 Builder、JSON 配置、最低等级过滤和远程备份不列入本轮。

## 7. 阅读顺序与下一步

| 顺序 | Kama 参考 | dlog 对照 | 重点 |
| --- | --- | --- | --- |
| 1 | [LogFlush.hpp][ref-flush] | [LogFile](../src/core/logfile.h)、[FileUtil](../src/utils/fileutil.h) | 输出接口、所有权、刷新和失败反馈 |
| 2 | [AsyncWorker.hpp][ref-worker]、[AsyncBuffer.hpp][ref-buffer] | [AsyncLogging](../src/core/asynclogging.cpp)、[Buffer](../src/core/buffer.h)、[FixedBuffer](../src/core/fixedbuffer.h) | 复制、交换、锁、唤醒与停止排空 |
| 3 | [AsyncLogger.hpp][ref-logger] | [Logger](../src/core/logger.h)、[LogStream](../src/core/logstream.h) | 长期对象如何接收临时记录 |
| 4 | [Manager.hpp][ref-manager]、[MyLog.hpp][ref-entry]、[实际示例][ref-example] | [现有异步示例](../snippet/AsyncLoggingTest/main.cpp) | 默认入口、命名管理和生命周期 |

**现在只实施第一步：让现有后台线程通过统一输出接口写终端或文件，不先改名 Logger。** 每步迁完调用方、测试和示例，验证后再进入下一步；提交说明不用类型前缀。

参考链接是动态分支；上述对比继承此前源码阅读结论。未运行 Kama、远程备份服务、压力或存储故障对测，不能把静态分析当成运行验证。

[ref-flush]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/LogFlush.hpp
[ref-worker]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncWorker.hpp
[ref-buffer]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncBuffer.hpp
[ref-logger]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncLogger.hpp
[ref-manager]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/Manager.hpp
[ref-entry]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/MyLog.hpp
[ref-example]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/examples/test.cpp
