# dlog 与 Kama 对比及简化迁移计划

## 1. 目标与进度

更新日期：2026-11-09；本轮改动前代码基线：`db911ec`。本文合并原对比文档与迁移计划，后续只维护这一份。

采用 Kama 的核心用法：**长期存在的 AsyncLogger 管理工作器和输出目标，使用者创建日志器后直接写日志。** 保留 dlog 的流式宏，先完成终端和文件输出，不扩展成通用日志框架。

- [x] 动态 FlexibleBuffer 接入 LogStream 和 Logger，超长记录完整交付。
- [x] 验证 5000 字节正文、空字节、颜色、边界换行及超过两个异步缓冲块的记录完整落盘。
- [x] LogFile 统一支持普通和轮转模式：单参数构造使用完整文件名，现有构造方式继续启用轮转。
- [x] StdoutFlush、FileFlush 实现 `writeAndFlush()`；LogFile 移除自动刷新策略，现有异步后端已通过 FileFlush 写出。
- [x] FileUtil 构造参数改为 filesystem::path，Windows 使用宽字符打开接口，保留 FILE* 和缓冲写入。
- [x] FileUtil 提供可执行文件路径及目录查询，相对路径以可执行文件目录为基准；LogFile 的轮转扫描与写入使用同一绝对路径。
- [x] 新增 RollFileFlush，复用 FileFlush 的写入与刷新及 LogFile 的轮转；两种输出类均接收 filesystem::path。
- [x] LogFlushFactory 创建输出对象，覆盖参数转发、共享生命周期和构造异常传播。
- [x] AsyncLogging 改用 RollFileFlush，构造参数和路径成员改为 filesystem::path；同步更新示例与测试，补充异步 Unicode 路径验证。FileFlush 的双参数构造已改为 protected。
- [x] FlexibleBuffer / FixedBuffer 及其测试迁入 utils，统一存储、读写区域和清空接口；FileUtil 使用 FixedBuffer 提供固定存储。
- [x] 可增长缓冲区命名为 FlexibleBuffer；固定容量常量及类型别名命名为 kSmallFixedBufferSize / kLargeFixedBufferSize、SmallFixedBuffer / LargeFixedBuffer，同步文件名、调用方及文档。
- [ ] 输出目标与异步工作器解耦。
- [ ] AsyncLogger、默认入口、自动启动关闭及 FATAL 排空。
- [ ] 简单命名管理和多个输出目标。

2026-11-09 已记录下文第一步中的 Worker 迁移方案：采用批次输出回调，保留现有固定块队列和启停行为。方案尚未实施，AsyncWorker、对应失败状态接口及新增验收项均未完成；此前测试结果仅覆盖现有代码。

此前 FlexibleBuffer 闭环验证为 Windows 全量 172 项测试通过。LogFile 双模式改造后核心 144 项通过；本轮补充 5 项 FileFlush 测试，Windows 核心 149 项通过。不代表下述待实施功能已经通过验收，日志器工厂等接口示例仍是待实现方案。

FileUtil 路径适配覆盖中文、空格与 emoji 文件名、空路径及内嵌空字符拒绝。新增静态方法 `executableFullPath()` 和类体内隐式 inline 的 `executableDirectory()`；Windows 使用 32768 个宽字符的缓冲区获取进程 EXE 路径，检测到截断时抛出 system_error，Linux 读取 `/proc/self/exe`。相对路径拼接可执行文件目录后做词法规范化，绝对路径不会拼接目录。

参考项目的 FileUtil 反复按路径查询文件，因此保存 m_path；当前 FileUtil 是打开后的追加写入器，只在构造时需要路径，暂不增加未使用的路径成员。LogFile 已使用 path 保存绝对轮转基准，并按 UTF-8 文件名匹配已有序号，避免更改工作目录或 Unicode 路径造成扫描和写入不一致。FileFlush、RollFileFlush、AsyncLogging 均已接收原生 path；缺失的父目录会在文件打开或轮转扫描前自动创建。

此前路径适配时 Windows 共 196 项测试，195 项通过，1 项因 MSVC 不支持负 time_t 而跳过，无失败；覆盖进程 EXE 路径查询、工作目录变化、相对路径词法规范化、绝对路径保留，以及相对目录和 Unicode 文件名下的轮转扫描。Linux 分支尚未运行验证。

此前 FileFlush / RollFileFlush 适配后，单独运行 Windows 核心 152 项测试，全部通过；覆盖两种输出的 Unicode 路径、空字节、写后立即可读、空写入、无效输入，以及轮转后重开保持旧内容。该轮未重跑工具测试，AsyncLogging 源码、测试和示例当时均未修改。

父目录自动创建后，Windows 全量 206 项测试中 205 项通过、1 项跳过，无失败，构建通过。覆盖多级 Unicode 目录、已有目录重开追加、可执行目录下的相对路径创建、普通及轮转输出，以及目录位置被文件占用时的异常传播与原文件保留。FileUtil 打开前创建 parent_path，LogFile 在轮转扫描前准备目录；不保存额外路径成员或增加单独的目录操作接口。

2026-11-09 完成 AsyncLogging 路径及输出类型迁移：路径参数和成员使用 filesystem::path，后台使用 RollFileFlush，FileFlush 的双参数构造收为 protected。测试及示例直接传入 path，异步测试按 UTF-8 文件名查找日志；示例和 README 移除预建目录步骤。本轮新增 2 项测试，覆盖中文、空格、emoji 目录及基础文件名、缺失父目录自动创建、轮转与重启保留旧内容，以及可执行文件目录下的相对 Unicode 路径和空字节。Windows 全量 208 项测试中 207 项通过、1 项因 MSVC 不支持负 time_t 而跳过，无失败，库、测试及异步示例构建通过。通用 AsyncWorker 和后台失败处理仍待实施。

本轮继续保留 FlexibleBuffer 与 FixedBuffer，将两类、实现及测试从 core 移到 utils。公共接口统一为 storageData/capacity、readableBegin/readableBytes、writableBegin/writableBytes、append、clear、toString；FlexibleBuffer 的 retrieve 系列改名为 consume 系列，retrieveAll 对应 consumeAll，内部复用 clear 并保留独立接口，FixedBuffer 保留 bzero 进行物理清零。FixedBuffer 超限追加抛出 length_error，正长度空指针抛出 invalid_argument，失败保留原内容。FileUtil 通过 FixedBuffer<64 * 1024> 的 storageData/capacity 借出固定存储给 setvbuf，文件流状态仍由 C 运行库管理，fclose 完成前不修改或释放存储。Windows 全量 216 项测试中 215 项通过、1 项跳过，无失败；覆盖共同接口约定、部分消费后地址区别、固定容量超限及小块文件写入跨 64 KiB 边界和关闭刷尾。头文件路径、成员接口和 FileUtil 对象布局发生变化，使用方需迁移并重新编译。

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
| 缓冲 | 单条记录用动态 FlexibleBuffer，后台用 LargeFixedBuffer 块队列 | 两个 vector 缓冲区交换；SAFE 等待、UNSAFE 扩容 | 保留 dlog 已验证的缓冲组织，不直接替换 |
| 唤醒 | 整块入队通知，未满数据周期处理，默认 3 秒 | 每次 Push 通知消费者 | 核心迁移先保留；终端低流量延迟需观察再调整 |
| 文件与轮转 | LogFile 统一管理普通文件与按日期、大小轮转的文件；FileUtil 补写部分写入 | FileFlush / RollFileFlush，按批次检查大小 | FileFlush / RollFileFlush 区分用途，底层仍由 LogFile 管理两种模式 |
| FATAL | 输出、刷新回调、abort；异步回调未自动接通 | 等待备份后提交异步输出，不主动终止进程 | 保留 dlog 的终止语义，退出前排空目标 |
| 配置及平台 | 构造参数；Windows / Linux 适配 | JSON、Builder、全局指针；偏向 POSIX | 本轮不迁配置、Builder、网络备份或线程池 |

原先 4000 字节 SmallFixedBuffer 导致正文整段丢失的问题已经修复；后端轮转只能处理已收到的数据，无法恢复前端曾经丢弃的字节。不要继续把旧问题列为未完成项。

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

`"app"` 是日志器名称，`"logs/app"` 是轮转模式的基础路径，实际文件类似 `logs/app.YYYYMMDD.0.log`。普通文件模式则使用完整文件名。创建函数准备目录、文件和工作器，失败在初始化处报告。调用方不再手动接 SetOutput、SetFlush 或调用后端 start。

LogFile 支持单参数普通模式、双参数轮转模式。输出层使用 FileFlush(filename) 或 RollFileFlush(basePath, rollSize)，后者继承前者，共用 writeAndFlush，不重复实现轮转。AsyncLogging 已使用 RollFileFlush，FileFlush 的双参数构造为 protected；直接调用旧入口的使用方需改用 RollFileFlush。AsyncLogging 路径接口及对象布局已改变，使用方需重新编译。普通模式重开时追加已有内容，LogFile 的 `rollFile()` 抛出 `std::logic_error`；两种模式都会自动创建缺失的父目录；目录创建失败或路径被非目录占用时抛出 filesystem_error。AsyncLogger 日志器工厂仍待实施，已有 LogFlushFactory 只创建输出对象。

刷新职责已经分开：LogFile 提供 `append()` 与 `flush()`，不再保存刷新间隔或自动刷新；FileFlush 将两次调用组合成 `writeAndFlush()`。AsyncLogging 保留定时等待，每个非空缓冲块作为一个输出批次，不再另外调用 flush。直接使用 LogFile 的调用方需显式刷新，原三参数构造需改为两参数并重新编译。

默认终端从同步改为异步后，调用返回不代表终端已经显示内容；正常退出需要排空。

## 4. 组件分工

| 组件 | 职责与迁移方式 |
| --- | --- |
| 现有 Logger / LogStream / FlexibleBuffer | 构造单条记录，完成后提交；保留格式、空字节和超长支持 |
| AsyncLogger | 长期持有工作器与输出目标，接收完整记录 |
| AsyncWorker | 从 AsyncLogging 提取，负责字节复制、缓冲队列、后台线程、周期处理、停止排空及失败记录；通过回调交付批次 |
| 过渡期 AsyncLogging | 保留轮转文件的便捷入口，管理路径、轮转大小和当前输出对象，启停及追加转发给同一个 Worker |
| LogFlush | 单一接口 `writeAndFlush(data, len)`：按显式长度写入并刷新；长度为 0 时直接返回 |
| StdoutFlush / FileFlush / RollFileFlush | 分别输出到终端、普通文件、轮转文件；文件类共用写入与刷新，轮转行为由 LogFile 决定 |
| 简单管理器 | 保存默认和命名 AsyncLogger，统一关闭 |

以下是目标数据流；批次回调由外层提供，输出对象由 AsyncLogger 或过渡期 AsyncLogging 管理。

```text
DLOG_INFO()
  → 临时 Logger + LogStream + FlexibleBuffer
  → 长期 AsyncLogger
  → AsyncWorker
  → 批次输出回调
  → StdoutFlush、FileFlush 或 RollFileFlush
```

每个 AsyncLogger 一个工作器，先不共享线程池。现有 FlexibleBuffer、LargeFixedBuffer、跨块追加和文件轮转继续复用；FixedBuffer 不承担日志截断、分片策略或警告输出。

## 5. 分三步实施

### 第一步：输出目标与工作器解耦

- 已定义 LogFlush 的 `writeAndFlush(data, len)` 接口，一次完成写入与刷新，实现终端和文件输出。FileFlush / RollFileFlush 区分普通与轮转用途，共用写入与刷新，轮转仍委托 LogFile。AsyncLogging 的 RollFileFlush 接入与 path 适配已完成，下一步提取通用工作器。后台按批调用，避免每条日志都单独刷新。
- 从 AsyncLogging 提取工作器，把写死的 RollFileFlush 改为调用批次输出回调，由外层将字节交给 LogFlush。Worker 不直接持有 LogFlush，只保留一套消费循环；原 AsyncLogging 调用在迁移期间继续通过该循环工作，不另写一个后端。
- 保留持锁整条追加、跨块顺序、锁外写出、周期刷新、重复启停和停止排空。
- 后台捕获失败、保存错误状态并直接向 stderr 报告，避免异常逃出线程或递归记录。FileUtil 已有的报错返回路径也要检查，单加 catch 不够。

验收：后台可输出到终端或文件；长记录、空字节、跨块、重启与排空测试通过；失败可识别，停止不会因自身状态或条件变量处理而遗漏线程回收或陷入等待。底层 I/O 永久阻塞时能否限时退出不在本步保证范围内。

涉及：`src/core/asynclogging.*`、新增 `src/core/asyncworker.h/.cpp`、现有 `logflush.*` 接入、必要的 `logfile.*` / `fileutil.*` 调整及对应测试、示例和公共头文件验证。

#### Worker 迁移方案（待实施）

**参考范围与选择。** 本方案参考 [Kama Worker][ref-worker]、[Kama Buffer][ref-buffer] 和 [Kama Logger][ref-logger] 的职责划分。Kama Worker 管理缓冲和线程，调用外部回调交付批次；Logger 持有输出目标并在 RealFlush 中实际输出。dlog 采用相同分工，但回调只暴露显式长度的字节范围，不暴露可修改的缓冲区对象。以下均为静态阅读后的迁移设计，未运行 Kama 验证。

保留现有 LargeFixedBuffer 块队列、整条记录持锁追加、跨块顺序、锁外输出及备用块复用；本步不改成 Kama 的双 vector 交换，也不增加 SAFE / UNSAFE 模式。FlexibleBuffer 继续用于前端记录拼接。现有整块入队通知、未满数据周期处理的策略先保持；以后调整终端低流量延迟时，需同时检查通知时机和等待谓词，不能仅增加 notify_one。

**接口草案。** 以下接口尚未实现，错误查询接口的具体命名可在实施前调整：

```cpp
class AsyncWorker
{
public:
    using OutputFunc = std::function<void(const char *, size_t)>;

    explicit AsyncWorker(OutputFunc output,
                         unsigned int flushInterval = 3);
    ~AsyncWorker();

    void append(const char *data, size_t len);
    void start();
    void stop();
    std::exception_ptr error() const;
};
```

| 接口 | 语义约定 |
| --- | --- |
| 构造 | 保存有效的输出回调、准备缓冲，不在构造时启动线程；空回调在初始化处拒绝 |
| append | 将输入字节复制到 Worker 自有缓冲，返回不代表输出完成；保留启动前追加能力，不保存调用方的临时数据指针 |
| start | 启动后台消费者；运行中重复调用不创建第二个线程，正常 stop 后允许重启；线程创建失败回滚运行状态 |
| stop | 正常情况下处理完已接收数据并等待线程退出；重复调用安全；输出失败时仍回收线程，但不能把失败当成成功排空 |
| error | 以同步方式返回后台保存的失败信息；失败不自动清除或重试，恢复策略另行设计 |
| 析构 | 停止并回收线程，后台输出异常不得逃出析构；不得依赖 Thread 的 detach 兜底 |

OutputFunc 在后台线程、释放队列锁后执行。数据指针仅在本次回调期间有效，回调应在返回前完成批次处理；若要交给其他线程，接收方必须自行复制。保留每个非空缓冲块作为一个输出批次，由输出类完成 writeAndFlush，不另加每条记录的同步刷新。Worker 的 start/stop 供内部控制，后续由 AsyncLogger 管理自动启动和关闭。

**成员与职责迁移。**

| 当前 AsyncLogging 的内容 | 目标位置 |
| --- | --- |
| m_basePath、m_rollSize | 留在过渡期 AsyncLogging 的文件输出封装层 |
| 创建及释放 RollFileFlush | 外层负责，移出 Worker 消费循环 |
| m_thread、运行及停止状态、m_mtx、m_cond | AsyncWorker |
| m_producerBuffer、m_pendingBuffers、周期等待间隔 | AsyncWorker |
| append 的跨块复制、队列交换、备用块复用、停止排空 | AsyncWorker，保留一套实现 |
| 对每个非空块调用 output.writeAndFlush | 改为调用 OutputFunc，由外层转交当前 LogFlush |

Worker 不保存路径、轮转大小、日志等级、源文件位置或日志器名称。AsyncLogger 或过渡期 AsyncLogging 持有 Worker 和输出对象。若回调引用外层对象，外层必须先 stop 并 join Worker，再销毁输出对象；不得捕获拥有自身的 shared_ptr 形成循环，也不得从后台输出回调调用 stop 导致线程 join 自身。配置和控制操作按现有约定串行执行。

**过渡期文件输出的生命周期。** 保留现有测试规定的“未启动不创建文件”“启动前追加可在启动后处理”“正常停止后重启新建轮转文件”：

```text
构造 AsyncLogging
  保存路径和轮转大小，准备 Worker 和回调，不打开文件
  ↓
start()
  首次启动或上一轮已停止时，在调用线程创建新的 RollFileFlush
  文件准备成功后启动 Worker；运行中的重复 start 不重建输出对象
  ↓
后台处理
  Worker 交付字节批次 → 回调 → 当前 RollFileFlush
  ↓
stop()
  请求 Worker 停止，正常情况下排空并 join，再释放当前输出对象
  ↓
再次 start()
  创建新的 RollFileFlush，重新扫描轮转序号，再启动 Worker
```

输出对象的初始化失败直接在 start 报告，不留给后台线程。该顺序应保留现有 CanRestartAfterStop 测试中两轮运行产生两个轮转文件的行为。外层不再另建缓冲或消费线程，避免形成两套后端；后续 AsyncLogger 创建资源并启动 Worker，继续沿用同一消费实现。

**失败处理与参考实现的限制。**

- Kama 的 SAFE 等待条件要求单次消息完整放入剩余空间；超出固定容量的消息可能一直等待，且等待条件未处理停止请求。dlog 继续使用跨块追加，不照搬该容量等待。
- Kama 的 UNSAFE 缓冲只扩容一次，不能保证扩容后能容纳任意长度追加；本步不迁移其扩容策略。
- Kama Stop 无条件 join，消费循环以 while (!stop_) 为入口条件，存在重复停止和停止时跳过待写数据的问题。dlog 保留停止请求已到且待写队列、生产者尾部均为空才正常退出的规则，缓冲状态判断遵循同一加锁约定。
- 后台捕获输出异常，保存失败信息并直接向 stderr 报告，停止继续接收无从处理的数据；不递归调用日志入口，不默默重试可能已部分输出的批次。
- 当前 stop 中的 `if (!m_running.exchange(false)) return;` 不能原样用于失败路径：后台失败可能先把运行标志置为 false，但线程仍需 join。应分别处理“是否继续消费”与“线程是否已启动、尚待回收”，即使运行标志已为 false 也必须回收已启动线程。
- FileUtil::append 当前 ferror 分支打印错误、清除标志后返回，必须补成上层可识别的失败反馈，并保留实际接受的字节统计。仅在 Worker 增加 catch 无法发现被底层吞掉的失败；异常不得越过后台线程入口。
- 正常停止保证处理完已接收数据；发生输出失败时，只能报告失败并安全回收资源，不能承诺剩余数据完整落盘，也不承诺底层 I/O 阻塞的退出时限。

**实施顺序与验收。**

1. 提取 AsyncWorker 并完成调用迁移：复用现有缓冲和消费循环，接入字节批次回调；AsyncLogging 改为薄封装，同步测试及示例。验证内存捕获、终端、普通文件和轮转文件输出；保留空追加、空字节、长记录、跨块顺序、并发追加、周期处理、重复启停、启动前追加、重启和停止排空等现有行为。
2. 补齐失败路径：处理输出异常、错误查询、失败后停止及析构回收、线程创建失败回滚，以及 FileUtil 部分写入和刷新失败。使用可控的抛异常输出目标验证错误可观察、线程可回收；检查停止后追加、失败后重启的接受规则并明确写入接口约定。两部分完成后才将本节第一步标为完成，再接 AsyncLogger。

每部分的实现、调用方、测试及相关文档应作为同一功能范围验收。实施与提交均等待用户明确指令，本次仅记录方案，不代表开始迁移或获得 Git 提交授权。

### 第二步：增加 AsyncLogger，打通默认使用

- AsyncLogger 负责创建资源、启动工作器和关闭排空；默认首次使用时创建终端日志器，提供文件创建函数、SetDefaultLogger 和 Shutdown。
- 临时 Logger 构造时取得目标 AsyncLogger 强引用，析构时提交一次；异步接收立即复制字节，后台不保存临时 FlexibleBuffer 指针。
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
| 2 | [AsyncWorker.hpp][ref-worker]、[AsyncBuffer.hpp][ref-buffer] | [AsyncLogging](../src/core/asynclogging.cpp)、[FlexibleBuffer](../src/utils/flexiblebuffer.h)、[FixedBuffer](../src/utils/fixedbuffer.h) | 复制、交换、锁、唤醒与停止排空 |
| 3 | [AsyncLogger.hpp][ref-logger] | [Logger](../src/core/logger.h)、[LogStream](../src/core/logstream.h) | 长期对象如何接收临时记录 |
| 4 | [Manager.hpp][ref-manager]、[MyLog.hpp][ref-entry]、[实际示例][ref-example] | [现有异步示例](../snippet/AsyncLoggingTest/main.cpp) | 默认入口、命名管理和生命周期 |

**本轮已完成 AsyncLogging 的 RollFileFlush 接入与 path 适配、缓冲区接口统一及 utils 模块迁移，并同步迁移测试、示例和 README；Worker 迁移方案已记录，尚未实施。后续按第五节方案提取基于批次回调的 AsyncWorker，保留现有固定块队列和文件启停行为，同时补齐后台失败反馈。** 不先改名 Logger。每步迁完调用方、测试和示例，验证后再进入下一步；仅在用户明确要求时提交 Git，按功能组织相关实现、调用方、测试和文档，提交说明不用类型前缀。

参考链接是动态分支；上述对比继承此前源码阅读结论。未运行 Kama、远程备份服务、压力或存储故障对测，不能把静态分析当成运行验证。

[ref-flush]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/LogFlush.hpp
[ref-worker]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncWorker.hpp
[ref-buffer]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncBuffer.hpp
[ref-logger]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncLogger.hpp
[ref-manager]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/Manager.hpp
[ref-entry]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/MyLog.hpp
[ref-example]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/examples/test.cpp
