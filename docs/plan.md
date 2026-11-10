# dlog 与 Kama 对比及简化迁移计划

## 1. 目标与进度

更新日期：2026-11-10；本次设计落档时的代码基线：`a3dc91f`。本文合并原对比文档与迁移计划，后续只维护这一份。

借鉴 Kama 的长期日志器用法。dlog 后续统一公开长期 `Logger`，由它管理工作器和输出目标；通过 `LoggerBuilder` 创建。业务侧继续使用流式宏，单条记录由内部 `LogMessage` 管理，`LogStream` 继续只负责格式化和缓冲。

**执行约定（2026-11-10）：先落文档，再逐步实施。** 步骤 1 已完成实现，按用户“复核无问题后分四到五批提交”的授权完成本轮复核，并分四批提交；未进入步骤 2。后续仍在每次开始前说明范围，完成后报告实际改动、验证结果及剩余限制，等待用户明确审核通过后再进入下一步。提交当前步骤不表示授权自动开始下一步；未获明确要求不提交 Git。

- [x] 动态 FlexibleBuffer 接入 LogStream 和 Logger，超长记录完整交付。
- [x] 验证 5000 字节正文、空字节、颜色、边界换行及超过两个异步缓冲块的记录完整落盘。
- [x] LogFile 统一支持普通和轮转模式：单参数构造使用完整文件名，现有构造方式继续启用轮转。
- [x] StdoutFlush、FileFlush 实现 `writeAndFlush()`；LogFile 移除自动刷新策略，现有异步后端已通过 FileFlush 写出。
- [x] FileUtil 构造参数改为 filesystem::path，Windows 使用宽字符打开接口，保留 FILE* 和缓冲写入。
- [x] FileUtil 提供可执行文件路径及目录查询，相对路径以可执行文件目录为基准；LogFile 的轮转扫描与写入使用同一绝对路径。
- [x] 新增 RollFileFlush，复用 FileFlush 的写入与刷新及 LogFile 的轮转；两种输出类均接收 filesystem::path。
- [x] LogFlushFactory 创建输出对象，覆盖参数转发、共享生命周期和构造异常传播。
- [x] AsyncLogging 构造参数和路径成员改为 filesystem::path，同步示例与测试并补充异步 Unicode 路径验证；当前旧后端直接使用 LogFile。FileFlush 的双参数构造已改为 protected。
- [x] FlexibleBuffer / FixedBuffer 及其测试迁入 utils，统一存储、读写区域和清空接口；FileUtil 使用 FixedBuffer 提供固定存储。
- [x] 可增长缓冲区命名为 FlexibleBuffer；固定容量常量及类型别名命名为 kSmallFixedBufferSize / kLargeFixedBufferSize、SmallFixedBuffer / LargeFixedBuffer，同步文件名、调用方及文档。
- [x] 独立 AsyncWorker 完成正常路径的缓冲、后台消费、回调输出及停止排空；旧调用链暂保留。
- [x] Worker 捕获后台异常，显式停止时重抛原异常。
- [x] FileUtil 报告流写入失败，旧 AsyncLogging 保留短写诊断和刷新已接收数据的行为。
- [x] 步骤 1：内部 LogMessage 已提取，保留旧入口；复核通过，按用户授权分批提交。
- [ ] 步骤 2：长期 Logger / LoggerBuilder 接通单目标 Worker，实现关闭排空和失败反馈。
- [ ] 步骤 3：验证显式 Logger 的 FATAL 排空及终止。
- [ ] 步骤 4：迁移默认流式宏和旧调用链，完成旧接口清理。
- [ ] 步骤 5：加入简单命名管理和统一关闭。
- [ ] 步骤 6：增加多个输出目标，完成发布接口验证。

2026-11-09 已完成独立 AsyncWorker 的正常排空和后台异常传递。现有 Logger、AsyncLogging 继续使用原调用链；长期日志器的职责和命名方案见第五节。

此前 FlexibleBuffer 闭环验证为 Windows 全量 172 项测试通过。LogFile 双模式改造后核心 144 项通过；本轮补充 5 项 FileFlush 测试，Windows 核心 149 项通过。不代表下述待实施功能已经通过验收，日志器工厂等接口示例仍是待实现方案。

FileUtil 路径适配覆盖中文、空格与 emoji 文件名、空路径及内嵌空字符拒绝。新增静态方法 `executableFullPath()` 和类体内隐式 inline 的 `executableDirectory()`；Windows 使用 32768 个宽字符的缓冲区获取进程 EXE 路径，检测到截断时抛出 system_error，Linux 读取 `/proc/self/exe`。相对路径拼接可执行文件目录后做词法规范化，绝对路径不会拼接目录。

参考项目的 FileUtil 反复按路径查询文件，因此保存 m_path；当前 FileUtil 是打开后的追加写入器，只在构造时需要路径，暂不增加未使用的路径成员。LogFile 已使用 path 保存绝对轮转基准，并按 UTF-8 文件名匹配已有序号，避免更改工作目录或 Unicode 路径造成扫描和写入不一致。FileFlush、RollFileFlush、AsyncLogging 均已接收原生 path；缺失的父目录会在文件打开或轮转扫描前自动创建。

此前路径适配时 Windows 共 196 项测试，195 项通过，1 项因 MSVC 不支持负 time_t 而跳过，无失败；覆盖进程 EXE 路径查询、工作目录变化、相对路径词法规范化、绝对路径保留，以及相对目录和 Unicode 文件名下的轮转扫描。Linux 分支尚未运行验证。

此前 FileFlush / RollFileFlush 适配后，单独运行 Windows 核心 152 项测试，全部通过；覆盖两种输出的 Unicode 路径、空字节、写后立即可读、空写入、无效输入，以及轮转后重开保持旧内容。该轮未重跑工具测试，AsyncLogging 源码、测试和示例当时均未修改。

父目录自动创建后，Windows 全量 206 项测试中 205 项通过、1 项跳过，无失败，构建通过。覆盖多级 Unicode 目录、已有目录重开追加、可执行目录下的相对路径创建、普通及轮转输出，以及目录位置被文件占用时的异常传播与原文件保留。FileUtil 打开前创建 parent_path，LogFile 在轮转扫描前准备目录；不保存额外路径成员或增加单独的目录操作接口。

2026-11-09 完成 AsyncLogging 路径及输出类型迁移：路径参数和成员使用 filesystem::path，后台使用 RollFileFlush，FileFlush 的双参数构造收为 protected。测试及示例直接传入 path，异步测试按 UTF-8 文件名查找日志；示例和 README 移除预建目录步骤。本轮新增 2 项测试，覆盖中文、空格、emoji 目录及基础文件名、缺失父目录自动创建、轮转与重启保留旧内容，以及可执行文件目录下的相对 Unicode 路径和空字节。Windows 全量 208 项测试中 207 项通过、1 项因 MSVC 不支持负 time_t 而跳过，无失败，库、测试及异步示例构建通过。当时通用 AsyncWorker 和后台失败处理仍待实施；本轮进度见下文第一步。

本轮继续保留 FlexibleBuffer 与 FixedBuffer，将两类、实现及测试从 core 移到 utils。公共接口统一为 storageData/capacity、readableBegin/readableBytes、writableBegin/writableBytes、append、clear、toString；FlexibleBuffer 的 retrieve 系列改名为 consume 系列，retrieveAll 对应 consumeAll，内部复用 clear 并保留独立接口，FixedBuffer 保留 bzero 进行物理清零。FixedBuffer 超限追加抛出 length_error，正长度空指针抛出 invalid_argument，失败保留原内容。FileUtil 通过 FixedBuffer<64 * 1024> 的 storageData/capacity 借出固定存储给 setvbuf，文件流状态仍由 C 运行库管理，fclose 完成前不修改或释放存储。Windows 全量 216 项测试中 215 项通过、1 项跳过，无失败；覆盖共同接口约定、部分消费后地址区别、固定容量超限及小块文件写入跨 64 KiB 边界和关闭刷尾。头文件路径、成员接口和 FileUtil 对象布局发生变化，使用方需迁移并重新编译。

## 2. 当前 dlog 与 Kama 的联系和区别

两边都是业务线程格式化并复制日志，后台线程批量输出；格式化、分配、复制和锁竞争仍发生在业务线程。异步不等于无锁，也不等于业务线程完全不等待。

| 方面 | 当前 dlog | Kama 参考实现 | 本轮选择 |
| --- | --- | --- | --- |
| 日志入口 | `DLOG_INFO() << value` | `Info(fmt, ...)` 及默认宏 | 保留流式宏与文件名、行号捕获 |
| 日志器含义 | 临时 Logger 构造一条记录 | 长期 AsyncLogger 接收多条记录 | 公开 Logger 负责长期资源，内部 LogMessage 复用现有记录格式化 |
| 异步接入 | 调用方创建 AsyncLogging 并设置全局回调 | AsyncLogger 内部创建工作器 | 库内部完成连接与生命周期管理 |
| 默认输出 | 同步 stdout | 默认日志器异步输出终端 | 迁为默认异步终端，属于可见的行为变化 |
| 输出目标 | 回调连接文件等后端 | LogFlush 接口及输出目标列表 | 先单目标，再支持列表 |
| 日志器管理 | 进程共享回调 | LoggerManager 保存默认和命名日志器 | 使用简单管理器 |
| 缓冲 | 单条记录用动态 FlexibleBuffer，后台用 LargeFixedBuffer 块队列 | 两个 vector 缓冲区交换；SAFE 等待、UNSAFE 扩容 | 保留 dlog 已验证的缓冲组织，不直接替换 |
| 唤醒 | 整块入队通知，未满数据周期处理，默认 3 秒 | 每次 Push 通知消费者 | 核心迁移先保留；终端低流量延迟需观察再调整 |
| 文件与轮转 | LogFile 统一管理普通文件与按日期、大小轮转的文件；FileUtil 补写部分写入 | FileFlush / RollFileFlush，按批次检查大小 | FileFlush / RollFileFlush 区分用途，底层仍由 LogFile 管理两种模式 |
| FATAL | 输出、刷新回调、abort；异步回调未自动接通 | 等待备份后提交异步输出，不主动终止进程 | 保留 dlog 的终止语义，退出前排空目标 |
| 配置及平台 | 构造参数；Windows / Linux 适配 | JSON、Builder、全局指针；偏向 POSIX | 使用小型 LoggerBuilder；不迁 JSON 配置、网络备份或线程池 |

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

两份参考的单条日志处理方式不同：[云存储版 AsyncLogger][ref-logger] 在日志函数中格式化正文，再构造 [LogMessage][ref-message] 并显式提交给 Worker；LogMessage 不负责析构提交。[kamaLog Logger][ref-kamalog-logger] 则持有 LogStream，在临时 Logger 析构时补后缀并输出。dlog 的方案结合长期日志器的资源管理与现有流式记录的析构提交；虽然两边同名，dlog 的 LogMessage 仍负责流式拼接和析构提交，职责与云存储版 LogMessage 不完全相同。

## 3. 迁移后怎样使用

以下均为待实现的用法草案，方法名在对应步骤内核对，不代表已有 API。创建入口采用 LoggerBuilder，不引入 Logger::Create 或 CreateFileLogger；输出对象仍由现有 LogFlushFactory 创建。第一批只实现显式单目标日志器，默认入口和统一关闭分后续步骤接入，见第五节。

显式异步文件日志器（步骤 2）：

```cpp
dlog::LoggerBuilder builder;
builder.setOutput(
    dlog::LogFlushFactory::CreateLogFlush<dlog::RollFileFlush>(
        std::filesystem::path("logs") / "app",
        64LL * 1024 * 1024));
auto app = builder.build();

DLOG_LOGGER_INFO(app) << "request=" << 42;

// 所有业务线程及临时记录结束后，显式关闭并处理可能的异常：
app->shutdown();
```

默认异步输出终端（默认宏在步骤 4 接入，全局 Shutdown 在步骤 5 接入）：

```cpp
DLOG_INFO() << "request=" << 42;

// 所有业务线程退出后：
dlog::Shutdown();
```

切换为异步文件日志器：

```cpp
// app 由上面的 LoggerBuilder 创建。
dlog::SetDefaultLogger(app);

DLOG_INFO() << "request=" << 42;

// 所有业务线程退出后：
app->shutdown();
```

`logs/app` 是轮转模式的基础路径，实际文件类似 `logs/app.YYYYMMDD.0.log`；这里尚未设置日志器名称。普通文件模式使用完整文件名。LogFlushFactory 创建输出对象时准备目录和文件，Builder 的 build() 创建 Logger 并启动 Worker；文件打开和线程创建失败在相应初始化调用处报告。调用方不再手动接 SetOutput、SetFlush 或调用后端 start。未注册的显式 Logger 由持有方关闭，不能假定全局 Shutdown 自动发现所有实例。

LogFile 支持单参数普通模式、双参数轮转模式。输出层使用 FileFlush(filename) 或 RollFileFlush(basePath, rollSize)，后者继承前者，共用 writeAndFlush，不重复实现轮转。以本次代码基线为准，旧 AsyncLogging 直接使用 LogFile，在追加和刷新之间处理流写入错误；前面的 RollFileFlush 迁移记录描述此前状态。FileFlush 的双参数构造为 protected；直接调用旧入口的使用方需改用 RollFileFlush。AsyncLogging 路径接口及对象布局已改变，使用方需重新编译。普通模式重开时追加已有内容，LogFile 的 `rollFile()` 抛出 `std::logic_error`；两种模式都会自动创建缺失的父目录；目录创建失败或路径被非目录占用时抛出 filesystem_error。长期 Logger 的创建接口仍待实施，已有 LogFlushFactory 只创建输出对象。

LogFile 分别提供 `append()` 和 `flush()`。FileFlush 将两次调用组合成 `writeAndFlush()`。旧 AsyncLogging 保留定时等待，每个非空缓冲块作为一个输出批次，并显式刷新。直接使用 LogFile 的调用方需显式刷新，原三参数构造需改为两参数并重新编译。

默认终端从同步改为异步后，调用返回不代表终端已经显示内容；正常退出需要排空。

## 4. 组件分工

| 组件 | 职责与迁移方式 |
| --- | --- |
| 内部 LogMessage（步骤 1 已实现并复核） | 承接现有临时 Logger 的职责，复用 LogStream / FlexibleBuffer，析构时补换行并提交一次 |
| 长期 Logger（待实现） | 持有工作器与输出目标，提供业务日志入口和显式关闭 |
| LoggerBuilder（待实现） | 收集构建配置，创建并启动长期 Logger；构建完成后不参与日志写入或关闭 |
| LogStream | 只负责 operator<< 格式化和本条缓冲，不持有输出目标，不在析构时提交 |
| AsyncWorker | 负责字节复制、缓冲队列、周期处理和正常排空；通过回调交付批次，显式停止时报告后台异常 |
| 现有 AsyncLogging | 目前保留原文件后端和调用链，后续迁移到长期 Logger |
| LogFlush | 单一接口 `writeAndFlush(data, len)`：按显式长度写入并刷新；长度为 0 时直接返回 |
| StdoutFlush / FileFlush / RollFileFlush | 分别输出到终端、普通文件、轮转文件；文件类共用写入与刷新，轮转行为由 LogFile 决定 |
| 简单管理器 | 保存默认和命名 Logger，统一关闭 |

以下是后续目标数据流，尚未接通。批次回调和输出对象由长期 Logger 管理。

```text
DLOG_LOGGER_INFO(app) 或迁移后的 DLOG_INFO()
  → 长期 Logger 创建内部 LogMessage
  → LogMessage 通过 LogStream 格式化，析构时提交给 Logger
  → AsyncWorker
  → 批次输出回调
  → StdoutFlush、FileFlush 或 RollFileFlush
```

每个异步 Logger 持有一个 Worker。现有 FlexibleBuffer、LargeFixedBuffer、跨块追加和文件轮转继续复用。

内部 LogMessage 接替的是当前临时 Logger 的位置，不再额外包一层新的临时对象。每个流式表达式独享 LogStream；长期 Logger 不保存供所有生产线程共同拼接的 LogStream。格式化在业务线程完成，记录正常结束时调用一次 append，后台只接收字节并按批次输出，不访问业务对象或重新格式化。

## 5. 实施阶段与逐步审核

### 第一步：独立 AsyncWorker

已完成 `src/core/asyncworker.h/.cpp` 和直接测试。原 AsyncLogging、测试和异步示例目前保留原调用链。Worker 复用原后端的固定块组织和消费流程，输出由批次回调决定。旧调用链的迁移随长期 Logger 实施。

**与 Kama 的对应关系。** [Kama Worker][ref-worker] 接收字节、管理缓冲和线程，再把批次交给回调；[Kama AsyncLogger][ref-logger] 持有输出目标并提供日志业务入口。dlog Worker 采用同一职责边界，保留现有 LargeFixedBuffer 块队列、整条记录持锁追加、跨块顺序、锁外输出和备用块复用，不移植 SAFE / UNSAFE 模式或双 vector 缓冲。Kama Worker 在构造时启动；本轮保留已审阅的显式 start/stop，后续由长期日志器内部管理，业务层不再重复暴露线程启停。参考源码仅作静态阅读，未运行 Kama 验证。

**已实现接口。**

```cpp
class AsyncWorker
{
public:
    using OutputFunc = std::function<void(const LargeFixedBuffer &)>;

    explicit AsyncWorker(OutputFunc output,
                         unsigned int flushInterval = 3);
    ~AsyncWorker();

    void append(const char *data, size_t len);
    void start();
    void stop();
};
```

| 接口 | 当前语义 |
| --- | --- |
| 构造 | 拒绝空回调，准备生产者缓冲，不启动线程；周期间隔单位为秒，0 在等待时按 1 秒处理 |
| append | 复制字节，可在首次启动前或正常停止后追加；空指针或零长度为空操作；后台失败后拒绝非空追加并重抛原异常 |
| start | 由调用方串行控制；运行中重复调用无效，正常停止后可重启；线程启动失败时回滚；已有后台异常时重抛 |
| stop | 未启动时不输出已缓存数据；调用前应结束生产者。正常停止时排空并 join；后台失败时先 join，再重抛原异常 |
| 析构 | 停止并回收线程，后台异常由显式 stop 报告 |

OutputFunc 在后台释放队列锁后执行，接收只读的 `const LargeFixedBuffer &`，通过 `readableBegin()` 和 `readableBytes()` 读取本批字节。缓冲区的清空和复用由 Worker 管理，引用及数据仅在本次回调期间有效；异步转交其他线程必须自行复制。每个非空块为一个批次；输出目标和回调捕获对象必须活到 Worker stop/join 完成之后。不得从回调中启停或销毁 Worker，避免线程 join 自身。控制操作串行执行，多个生产者可并发 append。

**当前状态与待设计事项。**

- 成员包括输出回调、周期间隔、运行标志、Thread、互斥锁、条件变量、生产者缓冲和待处理块队列。后台本轮消费的块及备用块留在线程局部变量中。
- 完整块入队后通知，未满数据按周期处理。停止状态的更新与等待谓词使用同一把锁，避免遗漏唤醒；正常退出要求停止请求已到、待处理队列和生产者尾部均为空。
- 后台入口保存原异常，停止消费并放弃剩余待写数据。失败前已输出的字节无法回滚；失败状态保留到 Worker 销毁。
- 前台轮换缓冲时先准备替换块，后台缺少备用块时也先分配再移交，避免分配失败后丢失生产者缓冲指针。
- FileUtil 遇到流写入错误时保留已接收的字节数，并抛出带错误码的异常。FileFlush 将异常传给 Worker。旧 AsyncLogging 在追加处捕获这类错误，打印后继续刷新已接收的前缀。
- 不移植 Kama 的容量等待：超出固定容量的消息可能一直等待，停止条件也需单独处理；不采用其单次扩容策略。停止及排空采用 dlog 当前规则。

**Worker 后续对齐（暂不实施，2026-11-10）。**

本次继续对照 Kama 的 Worker、Buffer 和 AsyncLogger 源码，仅记录后续方案，当前仍采用完整块入队通知、未满块周期处理。

1. 优先评估“有数据即可处理”的消费策略。Kama 每次追加后通知消费者；当前低流量下，一条未填满缓冲块的日志可能接近默认 3 秒后才输出。后续若调整，应同时让 append 在有效追加后解锁通知，并让后台等待谓词检查生产者缓冲区非空；只增加 notify_one 无法让当前谓词通过。后台仍一次取走当时积累的数据，不保证每次追加对应一次回调。届时一并明确周期间隔是否保留及其用途，比较终端延迟、小批次回调和文件刷新次数，并验证未满块及时处理、跨块顺序、停止排空及异常回收。
2. 核心用法完成后再设计积压上限和满载策略。LargeFixedBuffer 只限制单块容量，待处理队列仍可持续增长；先确定满载时阻塞、丢弃或报告失败，再实现机制和测试。借鉴 Kama 的容量等待思路，但不直接移植其 SAFE / UNSAFE：等待需要覆盖停止和失败，超大消息也必须能结束处理。
3. 线程生命周期随 Logger 接入处理。保留 Worker 的显式 start/stop，由后续长期 Logger 内部管理启动和关闭，业务侧不重复操作 Worker；暂不改为构造自动启动。

继续保留固定块队列和线程局部的 buffersToWrite、replaceBuffer，无需改成 Kama 的两个成员缓冲区。回调已使用 const LargeFixedBuffer &，缓冲区清空和复用由 Worker 管理；正常排空、重复停止安全及后台异常传递也继续保留。上述对比来自静态阅读，未运行 Kama，消费策略变更的吞吐影响尚未测量。

**验证范围。** Worker 测试覆盖输入复制、跨块和并发追加、周期处理、重复启停、正常排空、终端和文件回调，以及回调主动抛错后的异常传递。线程创建失败的回滚、Linux、持续压力和真实存储故障尚未验证。

加入异常用例后，Windows 使用 MSVC 直接构建并运行测试。静态链接和 DLL 两种构建均为 228 项通过、1 项因 MSVC 不支持负 time_t 而跳过。

### 第二步：统一 Logger（后续实施）

本节保留原阶段标题，便于已有链接继续定位；下面的“步骤 1～4”是本次确认的实施和审核单元。前面的独立 AsyncWorker 已完成，不计入这六个后续步骤。步骤 1 已实现并复核，步骤 2～6 均待实施；不能因本文列出接口或验收条件而视为完成。

#### 已确认的设计

现有 `Logger` 是单条记录的临时对象，构造时保存时间、等级、源文件和行号，用 LogStream / FlexibleBuffer 拼接正文，析构时补换行并调用全局输出。改造后公开名称统一为长期 `Logger`，现有临时对象的职责移入内部 `LogMessage`，通过 LoggerBuilder 构建长期对象。同一个 Logger 接收多条记录，每条流式表达式各自使用一个 LogStream。

| 层次 | 职责 | 接口边界 |
| --- | --- | --- |
| 内部 LogMessage | 记录创建时绑定目标和格式配置，保存元数据、流式拼接、析构时补换行并提交一次 | 向流式表达式提供本条 LogStream |
| 公开 Logger | 创建记录，持有一个 Worker 和一个 LogFlush，管理启动和关闭 | 提供日志入口和显式关闭，关闭时排空并报告失败 |
| LoggerBuilder | 接收构建配置，创建并启动 Logger | build 返回 shared_ptr<Logger>；不参与运行期写入 |
| LogStream | 沿用现有 operator<< 和 FlexibleBuffer | 只负责格式化和缓冲，不接管记录元数据或析构提交 |
| AsyncWorker | 复制字节、排队、后台交付、正常排空和后台异常传递 | append/start/stop，仅用于工作器控制 |
| LogFlush | 将收到的字节写到指定目标 | writeAndFlush；文件路径和轮转配置在对应输出对象中处理 |

**创建与配置。** 统一通过 LoggerBuilder 创建，Logger 不提供另一个静态 Create 入口；由 Builder 调用非公开构造。第一版只接一个输出目标，复用 LogFlushFactory，不同时提供另一套模板输出工厂。未指定输出时，build 为本次构建选择默认 StdoutFlush，不修改 Builder 自身配置；显式空目标应拒绝。Logger 接收独立的配置快照，后续修改 Builder 不影响已创建的实例。默认终端启用颜色，文件使用纯文本，颜色配置在创建后固定；具体 setter 名称、颜色与周期间隔参数在步骤 2 核对，不提前增加名称、Builder 继承体系或 SAFE / UNSAFE 模式。build 启动 Worker 后返回，业务侧不操作 start/stop；线程启动完成不等于后台已处理数据。

**所有权。** LogMessage 持有目标 Logger 的 shared_ptr，直到提交结束；释放外部指针不会令尚存活的记录悬空，但也不代表已显式关闭。Logger 持有 Worker，输出回调按值捕获 shared_ptr<LogFlush>，直接调用 writeAndFlush，不捕获 Logger 的强引用或依赖裸 this。Worker 排空并 join 后才能释放输出。LogMessage 本身作为临时对象存在，不要求单独堆分配。

**关闭与错误。** 显式 shutdown 负责关闭接收入口、正常排空、join，并反馈失败；成功后的重复关闭不重复回收，失败后的重复关闭仍能反馈已保存异常。关闭后不再接收新记录或缓存等待重启，Logger 第一版不提供重启。普通关闭前由调用方结束生产线程和临时记录，不承诺任意线程并发控制；FATAL 的并发协调在步骤 3 专门处理。LogMessage 析构中的补换行、提交异常不得逃出：保存首个前台失败并作最小 stderr 诊断，显式关闭还要反馈该错误；后台异常沿用 Worker 的保存和重抛机制。Logger 析构兜底回收、不向外抛异常，错误诊断不得递归调用日志宏。operator<< 自身的异常仍可由调用方捕获，不承诺格式化或输出失败后日志完整。

**提交与格式。** 时间、等级、源位置、errno、换行及显式字节长度沿用现有实现；正常记录格式化一次、调用一次 append，不另建独立的消息序列化路径。记录创建时绑定目标及颜色。宏仍捕获调用位置，目标表达式和 savedErrno 参数只求值一次，空目标在入口检查。跨块记录的顺序由 Worker 保证，不承诺分配失败时整条提交具有回滚能力，也不自动重试可能已部分接收或写出的记录。

#### 步骤 1：提取内部 LogMessage，保留当前入口

- **做什么：** 将当前 Logger / LoggerImpl 的单条记录职责移入内部 LogMessage，继续使用原 LogStream 和 FlexibleBuffer。临时兼容入口复用同一实现，保留现有默认宏、直接构造及 SetOutput/SetFlush，使本步可独立构建和审阅；不复制两套格式化代码，不接入新的 Worker 链路。
- **协调事项：** 过渡链路在记录创建时绑定输出、刷新回调及颜色快照，避免构造和析构分读全局目标；该过渡连接在步骤 4 清理，不作为新的公共回调扩展接口。检查 LogColorGuard 对 LogLevel/Logger 头文件的依赖，避免循环包含。
- **涉及：** logger.*、内部记录声明和实现、logger_test.cpp，以及必要的类型依赖；内部记录的具体文件布局按现有结构确定，不新增并行格式化框架。
- **验收：** 原有宏和直接构造仍可用；表达式结束时恰好提交一次；时间/等级/位置/errno/颜色/换行保持；空字节、5000 字节及跨多个块的长记录不截断；已有 FATAL 终止行为不回退。加入回调切换与记录绑定的验证，重新构建并运行受影响的核心测试。
- **审核点：** 只迁移单条记录职责，LogStream 仍可独立使用。报告结果后停止，等用户审核通过再开始步骤 2。

**本步实施结果（2026-11-10，复核通过）。** 新增 src/core/logmessage.h/.cpp，内部类型为 dlog::LogMessage，内部用途在说明和注释中表达；现有 logger.h/.cpp 文件和 Logger 类型保留，构造时绑定回调快照，stream 与析构提交复用同一份记录实现。FileNameView、LogLevel 和 LogLevelColorMode 随记录声明迁入 logmessage.h，原 logger.h 通过包含继续提供这些类型；LogColorGuard 的实现直接依赖记录声明，避免反向依赖兼容 Logger。现有宏、直接构造、SetOutput/SetFlush 和旧 AsyncLogging 调用链继续可用；对象布局已变化，使用方需重新编译。未增加长期 Logger、Builder 或新的 Worker 调用链。

新增 7 项测试，覆盖内部记录的长二进制消息和单次提交、空回调拒绝、原目标及颜色绑定、FATAL 刷新回调绑定，以及输出/刷新回调主动抛错。记录析构捕获失败并直接向 stderr 诊断，FATAL 即使输出失败仍尝试刷新后 abort；当前没有长期 Logger 的关闭接口，不提供关闭时查询或重抛错误，留到步骤 2。回调快照只复制 std::function，引用捕获资源仍由调用方保证有效。

Windows / MSVC / Debug / DLL：库构建成功，核心 140 项全部通过，包含既有跨多个异步缓冲块的长记录落盘用例。本轮复核运行 xmake test -y：共 238 项，237 项通过、1 项因 MSVC 不支持负 time_t 而跳过，无失败；数量按各组 GTest 的实际结果统计，未采用将跳过项误计为通过的脚本汇总行。LoggerTest 和 AsyncLoggingTest 两个原有示例均构建并运行成功。本轮未验证 Linux、静态链接、持续压力和真实存储故障。

复核按“多个生产线程、配置在开始产生日志前完成、禁止并发修改全局回调”的现有约定检查调用方、格式化、回调快照、析构异常、FATAL 和导出头文件依赖，未发现阻碍步骤 1 提交的问题。按用户要求分四批：独立 LogMessage 与共用类型及测试、Logger 委托与回归测试、README 使用说明、迁移计划与复核记录。第一批暂存内容另导出到独立目录，显式指定项目路径后完成配置、构建和核心测试，138 项全部通过；确认拆分不依赖第二批改动才能构建。继续停在步骤 1，待用户指示后才开始步骤 2。

#### 步骤 2：长期 Logger / LoggerBuilder 与单目标闭环

- **做什么：** 将公开 Logger 改为长期对象，加入小型 LoggerBuilder、单个输出目标、内部启动的 Worker 和显式 shutdown。接入非 FATAL 的显式流式入口，补齐 savedErrno 用法。此时迁移直接构造旧临时 Logger 的源码调用方和测试；默认宏及静态回调暂通过步骤 1 的兼容链路工作，不要求它们提前切到新后端。
- **协调事项：** 落实记录持有 Logger、回调持有 LogFlush 的所有权；构建失败清理；关闭拒绝、重复关闭、后台失败和前台析构提交失败反馈。对旧直接构造语法的变化作迁移说明，使用方需重新编译。文件路径和轮转仍留在具体输出对象中，不改 FileUtil / LogFile 的已验证语义。
- **涉及：** logger.*、内部记录、Builder、显式宏、Logger/Worker 集成测试及显式文件示例；README 仅补充本步已可用的接口。
- **验收：** 短记录写完立即关闭即可完整读取；普通文件、轮转文件及终端单目标均能连接；长记录和空字节完整；并发记录不交错；多个 Logger 不串写；释放外部指针时未完成记录仍有效；输出主动抛错可由 shutdown 捕获且原异常不丢失；重复关闭不重复输出；已关闭实例拒绝使用；修改 Builder 不改变旧实例。构建并运行相关核心及集成测试，检查 DLL 所需导出。
- **审核点：** 先审单目标正常使用、生命周期和错误闭环；本步不提前公开尚未验收的异步 FATAL 宏。结果提交用户审核后停止。

#### 步骤 3：显式 Logger 的 FATAL 排空

- **做什么：** 接入显式 FATAL / SYS_FATAL：提交致命记录，关闭该 Logger 的接收入口，排空此前已接收数据及致命记录，join 后 abort。协调仍在运行的生产者和多个关闭请求，避免继续追加、重复 join 或 join 自身；等待线程退出时不持有提交所需的锁。不依赖普通退出前已结束生产者的前提来实现 FATAL。
- **协调事项：** 当前 SetFlush 默认只刷新 stdout，不能复用为异步排空；writeAndFlush 的零长度调用也不承担排空。后台或提交失败时先作 stderr 诊断，再终止，不让异常逃出析构，也不宣称失败后仍可完整写盘。只保证目标 Logger 的已接收数据，不承诺排空其他 Logger。
- **涉及：** Logger 关闭协调、记录 FATAL 路径、显式宏和子进程集成测试。
- **验收：** 子进程终止后文件包含此前普通记录及致命记录；savedErrno 保留；输出失败仍有诊断并终止；有并发生产者或关闭请求时无死锁、无重复回收。不能只用“进程退出了”的断言替代文件检查。
- **审核点：** 用户确认 FATAL 语义与子进程证据后，再进入默认入口迁移。

#### 步骤 4：默认入口迁移和旧链路清理

- **做什么：** 保留 DLOG_INFO() 等调用形式，改为从默认 Logger 创建 LogMessage；接入已验收的 FATAL 路径和默认目标切换。默认目标为异步终端，本阶段可通过默认 Logger 的 shutdown 显式关闭，全局 Shutdown 在步骤 5 收口。
- **协调事项：** 退役 SetOutput/SetFlush 和旧临时 Logger 构造入口，自定义输出通过 LogFlush 接入。迁移现有异步示例、README 及测试后，移除旧 AsyncLogging 的重复队列/线程实现和临时兼容连接；不保留两个长期并行的后端。旧接口删除属于源代码和 ABI 变化，提供对应的新用法，并要求重新编译。
- **默认目标与延迟：** 生产线程启动前完成配置，不承诺并发热切换。记录创建时绑定目标；切换本身不提前关闭旧目标，旧目标由持有方在记录结束后关闭。默认终端迁移前必须明确未满块的延迟：若本步采用“有数据即可处理”，同时修改 append 通知和后台谓词，并验证顺序、停止与失败；若保留周期策略，则在公开用法中明确延迟，不能宣称日志立即显示。默认终端首次使用的初始化失败也须有明确反馈。
- **涉及：** 默认入口、logger.*、旧 asynclogging.* 及相关调用方、snippet、README、计划文档和测试；核对清理后的构建及导出。
- **验收：** 默认六级与系统错误宏可用；有序切换目标时未结束记录仍交给原目标且颜色正确；默认终端正常退出排空；新异步示例可运行；旧入口在源码和公开文档中的遗留已清理；若调整 Worker 唤醒则覆盖未满块及时处理。运行受影响的核心、工具和集成测试。
- **审核点：** 用户审核默认行为变化、旧接口迁移和清理结果后，再增加管理功能。

### 第三步：简单命名管理与多个输出目标

#### 步骤 5：简单命名管理与统一关闭

- **做什么：** 小型管理器保存默认和命名 Logger；在 Builder 中增加实际需要的名称配置，并提供明确注册和查询入口。重复名称拒绝，查不到返回空指针，显式日志入口仍检查空目标。配置和默认切换在业务线程启动前完成。
- **统一关闭：** Shutdown 关闭默认及已注册的日志器，对同一实例去重；一个实例失败仍尝试关闭其他实例，完成回收后反馈失败。未注册的显式 Logger 由调用方关闭。重复 Shutdown 安全，关闭后不自动重建默认 Logger，也不支持全局重新初始化。
- **涉及：** 管理器、Builder 的名称配置、默认入口、README 和命名/关闭集成测试。
- **验收：** 名称唯一、查询及空目标行为明确；不同日志器内容独立；默认与命名指向同一实例时不重复处理；有失败仍回收其他实例；重复关闭和退出时无悬空回调。
- **审核点：** 审核命名与全局生命周期，尚不增加多输出列表。

#### 步骤 6：多个输出目标与发布验证

- **做什么：** 输出目标扩为列表，Builder 对应增加追加目标的入口，沿用同一个 Worker，后台按顺序交付每个目标；同批一个目标失败仍尝试其他目标，再反馈错误。Worker 保存异常后停止消费并放弃剩余批次的行为不变，不增加自动重试。慢目标会拖慢该 Logger。
- **格式与边界：** 混合终端与文件统一纯文本；只有终端输出时保留颜色。每目标独立格式、等级过滤、共享线程池、JSON 和网络备份仍不加入。
- **涉及：** Builder、Logger 批次回调、输出集成测试、README、snippet，以及 DLL 导出和安装后的公共头文件验证。
- **验收：** 正常情况下同一字节批次恰好交给每个配置目标一次，文件无 ANSI；同批一个目标失败仍尝试其余目标，关闭能反馈错误；完整构建和适用的全量测试通过；安装后的外部示例能仅通过公开头文件及库使用新接口。不能用结构检查替代行为验证，未运行的平台和压力场景继续列明。
- **审核点：** 报告最后一批实现和验证，等待用户审核；通过后才将对应待办标记为完成。

**每步交付统一要求：** 说明做了什么、为何这样改、实际运行哪些验证、哪些没有验证以及接口迁移风险；同步本文状态但区分“实现完成待审核”和“用户审核通过”。若发现必须超出当前步骤的接口或行为变化，先整理可审阅的具体调整，不自行跳到后续步骤。

## 6. 本轮边界与后续事项

- 关闭前先结束日志生产者及临时记录；显式 Shutdown 是正常退出路径，析构兜底。第一版不承诺运行中并发切换配置或全局关闭后重新初始化。
- 完成接收、完成 fflush、完成系统存储同步是不同阶段。本轮只保证文件层刷新，不承诺断电持久性，也不直接迁 Kama 的 fsync 配置。
- 待写队列仍无总量上限。核心用法完成后再单独定义积压上限和满载策略，用可控慢输出测试超大消息、并发、停止和失败，不照搬可能永久等待的 SAFE 模式。
- 现有 LogFile 允许空文件接收超限批次；跨块记录可能跨文件，不承诺文件大小硬上限或按行轮转。
- 性能优化等测量后再做；比较前对齐消息量、线程数和刷新语义，同时核对实际字节、记录数、峰值内存与停止耗时。
- 只引入创建 Logger 所需的小型 Builder；同步框架、共享线程池、Builder 继承体系、JSON 配置、最低等级过滤和远程备份不列入本轮。

## 7. 阅读顺序与下一步

| 顺序 | Kama 参考 | dlog 对照 | 重点 |
| --- | --- | --- | --- |
| 1 | [LogFlush.hpp][ref-flush] | [LogFile](../src/core/logfile.h)、[FileUtil](../src/utils/fileutil.h) | 输出接口、所有权、刷新和失败反馈 |
| 2 | [AsyncWorker.hpp][ref-worker]、[AsyncBuffer.hpp][ref-buffer] | [AsyncWorker](../src/core/asyncworker.cpp)、[现有 AsyncLogging](../src/core/asynclogging.cpp)、[FixedBuffer](../src/utils/fixedbuffer.h) | 复制、交换、锁、唤醒与停止排空 |
| 3 | [AsyncLogger.hpp][ref-logger] | [Logger](../src/core/logger.h)、[LogStream](../src/core/logstream.h) | 长期对象如何接收临时记录 |
| 4 | [Manager.hpp][ref-manager]、[MyLog.hpp][ref-entry]、[实际示例][ref-example] | [现有异步示例](../snippet/AsyncLoggingTest/main.cpp) | 默认入口、命名管理和生命周期 |

**独立 AsyncWorker 已完成；步骤 1 的内部 LogMessage 已实现、复核并按用户授权分批提交。** 现有 Logger 文件与入口保留，尚未实施长期 Logger / Builder。下一步为第五节步骤 2，等待用户指示后再开始；之后仍逐步审核。仅在用户明确要求时提交 Git，按功能组织相关实现、调用方、测试和文档，提交说明不用类型前缀。

参考链接是动态分支；上述对比继承此前源码阅读结论。未运行 Kama、远程备份服务、压力或存储故障对测，不能把静态分析当成运行验证。

[ref-flush]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/LogFlush.hpp
[ref-worker]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncWorker.hpp
[ref-buffer]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncBuffer.hpp
[ref-logger]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncLogger.hpp
[ref-message]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/Message.hpp
[ref-kamalog-logger]: https://github.com/youngyangyang04/kamaLog/blob/main/logger/Logger.cc
[ref-manager]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/Manager.hpp
[ref-entry]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/MyLog.hpp
[ref-example]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/examples/test.cpp
