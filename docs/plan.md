# dlog 与 Kama 对比及简化迁移计划

## 1. 目标与进度

更新日期：2026-11-10；本次设计落档时的代码基线：`a3dc91f`。本文合并原对比文档与迁移计划，后续只维护这一份。

借鉴 Kama 的长期日志器用法。长期对象命名为 `AsyncLogger`，由它管理工作器和输出目标，通过 `AsyncLoggerBuilder` 创建；两者放在 asynclogger.h/.cpp 中，现有临时 `Logger` 暂时保留兼容。业务侧继续使用流式宏，单条记录由内部 `LogMessage` 管理，`LogStream` 继续只负责格式化和缓冲。

**执行约定（2026-11-10）：先落文档，再逐步实施。** 步骤 1 已完成实现，按用户“复核无问题后分四到五批提交”的授权完成本轮复核，并分四批提交；当前按下述小步推进步骤 2。后续仍在每次开始前说明范围，完成后报告实际改动、验证结果及剩余限制，等待用户明确审核通过后再进入下一步。提交当前步骤不表示授权自动开始下一步；未获明确要求不提交 Git。

**第二步细化（2026-11-10）：** 按用户要求拆为 2.1～2.6，每次只实施一个小步，完成并经用户审核通过后再继续。2.1 已实现并通过核心验证；用户现已指示继续 2.2，并要求 Builder 合并进 asynclogger 文件且改名 AsyncLoggerBuilder。当前 2.2 实现及本步验证完成，待审核；2.3～2.6 尚未开始。

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
- [ ] 步骤 2：长期 AsyncLogger / AsyncLoggerBuilder 接通单目标 Worker，默认自动回收，可选 stop 反馈错误；2.1 已获继续指示，2.2 实现完成待审核，2.3～2.6 待实施。
- [ ] 步骤 3：验证显式 AsyncLogger 的 FATAL 排空及终止。
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
| 日志器含义 | 临时 Logger 构造一条记录 | 长期 AsyncLogger 接收多条记录 | 公开 AsyncLogger 负责长期资源，内部 LogMessage 复用现有记录格式化 |
| 异步接入 | 调用方创建 AsyncLogging 并设置全局回调 | AsyncLogger 内部创建工作器 | 库内部完成连接与生命周期管理 |
| 默认输出 | 同步 stdout | 默认日志器异步输出终端 | 迁为默认异步终端，属于可见的行为变化 |
| 输出目标 | 回调连接文件等后端 | LogFlush 接口及输出目标列表 | 先单目标，再支持列表 |
| 日志器管理 | 进程共享回调 | LoggerManager 保存默认和命名日志器 | 使用简单管理器 |
| 缓冲 | 单条记录用动态 FlexibleBuffer，后台用 LargeFixedBuffer 块队列 | 两个 vector 缓冲区交换；SAFE 等待、UNSAFE 扩容 | 保留 dlog 已验证的缓冲组织，不直接替换 |
| 唤醒 | 整块入队通知，未满数据周期处理，默认 3 秒 | 每次 Push 通知消费者 | 核心迁移先保留；终端低流量延迟需观察再调整 |
| 文件与轮转 | LogFile 统一管理普通文件与按日期、大小轮转的文件；FileUtil 补写部分写入 | FileFlush / RollFileFlush，按批次检查大小 | FileFlush / RollFileFlush 区分用途，底层仍由 LogFile 管理两种模式 |
| FATAL | 输出、刷新回调、abort；异步回调未自动接通 | 等待备份后提交异步输出，不主动终止进程 | 保留 dlog 的终止语义，退出前排空目标 |
| 配置及平台 | 构造参数；Windows / Linux 适配 | JSON、Builder、全局指针；偏向 POSIX | 使用小型 AsyncLoggerBuilder；不迁 JSON 配置、网络备份或线程池 |

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

以下是完整用法草案：BuildLoggerFlush、Build 和正常 stop 已实现，消息入口、显式宏、默认异步入口及统一关闭仍待后续小步接通。创建入口采用 AsyncLoggerBuilder 的 BuildLoggerFlush<T>(...) 和 Build()，前者内部复用现有 LogFlushFactory；不增加 setOutput、Logger::Create、CreateFileLogger 或实例 shutdown 的同义入口。先实现显式单目标日志器，默认入口和统一关闭分后续步骤接入，见第五节。

显式异步文件日志器（步骤 2）：

```cpp
dlog::AsyncLoggerBuilder builder;
builder.BuildLoggerFlush<dlog::RollFileFlush>(
    std::filesystem::path("logs") / "app",
    64LL * 1024 * 1024);
auto app = builder.Build();

DLOG_LOGGER_INFO(app) << "request=" << 42;

// 结束所有生产线程及临时消息后，最后一个持有者释放 app 时自动排空回收。
// 如果需要提前等待写完、捕获输出异常，可在此处显式调用 app->stop()。
```

默认异步输出终端（默认宏在步骤 4 接入，全局 Shutdown 在步骤 5 接入；此处展示主动确定退出时点的用法）：

```cpp
DLOG_INFO() << "request=" << 42;

// 所有生产线程及临时消息结束后，可主动统一排空并获取错误：
dlog::Shutdown();
```

切换为异步文件日志器：

```cpp
// app 由上面的 AsyncLoggerBuilder 创建。
dlog::SetDefaultLogger(app);

DLOG_INFO() << "request=" << 42;

// 如需主动停止，在所有生产线程及临时消息结束后调用：
// app->stop();
```

`logs/app` 是轮转模式的基础路径，实际文件类似 `logs/app.YYYYMMDD.0.log`；这里尚未设置日志器名称。普通文件模式使用完整文件名。BuildLoggerFlush 内部调用 LogFlushFactory 准备输出目标、目录和文件，Build 创建 AsyncLogger 并启动 Worker；失败在相应初始化调用处报告。正常使用不要求手动调用 stop，也不再手工接 SetOutput/SetFlush 或启停 Worker。自动回收发生在最后一个 AsyncLogger 持有者释放时；局部 shared_ptr 离开作用域时若管理器或消息仍持有它，不会提前析构。未注册的显式 AsyncLogger 由持有方管理，不能假定全局 Shutdown 自动发现所有实例。

LogFile 支持单参数普通模式、双参数轮转模式。输出层使用 FileFlush(filename) 或 RollFileFlush(basePath, rollSize)，后者继承前者，共用 writeAndFlush，不重复实现轮转。以本次代码基线为准，旧 AsyncLogging 直接使用 LogFile，在追加和刷新之间处理流写入错误；前面的 RollFileFlush 迁移记录描述此前状态。FileFlush 的双参数构造为 protected；直接调用旧入口的使用方需改用 RollFileFlush。AsyncLogging 路径接口及对象布局已改变，使用方需重新编译。普通模式重开时追加已有内容，LogFile 的 `rollFile()` 抛出 `std::logic_error`；两种模式都会自动创建缺失的父目录；目录创建失败或路径被非目录占用时抛出 filesystem_error。长期 AsyncLogger 的创建接口已在 2.2 实现；消息提交仍待接通，LogFlushFactory 只创建输出对象。

LogFile 分别提供 `append()` 和 `flush()`。FileFlush 将两次调用组合成 `writeAndFlush()`。旧 AsyncLogging 保留定时等待，每个非空缓冲块作为一个输出批次，并显式刷新。直接使用 LogFile 的调用方需显式刷新，原三参数构造需改为两参数并重新编译。

默认终端从同步改为异步后，调用返回不代表终端已经显示内容；正常退出需要排空。

## 4. 组件分工

| 组件 | 职责与迁移方式 |
| --- | --- |
| 内部 LogMessage（步骤 1 已实现并复核） | 承接现有临时 Logger 的职责，复用 LogStream / FlexibleBuffer，析构时补换行并提交一次 |
| 长期 AsyncLogger（2.2 实现完成，待审核） | 持有工作器与输出目标，消息入口留待 2.3，析构时自动排空回收；stop 用于主动等待及错误反馈 |
| AsyncLoggerBuilder（与 AsyncLogger 放在同一文件） | 已支持单输出配置与 Build；之后不参与日志写入或关闭 |
| LogStream | 只负责 operator<< 格式化和本条缓冲，不持有输出目标，不在析构时提交 |
| AsyncWorker | 负责字节复制、缓冲队列、周期处理和正常排空；通过回调交付批次，显式停止时报告后台异常 |
| 现有 AsyncLogging | 目前保留原文件后端和调用链，后续迁移到长期 AsyncLogger |
| LogFlush | 单一接口 `writeAndFlush(data, len)`：按显式长度写入并刷新；长度为 0 时直接返回 |
| StdoutFlush / FileFlush / RollFileFlush | 分别输出到终端、普通文件、轮转文件；文件类共用写入与刷新，轮转行为由 LogFile 决定 |
| 简单管理器 | 保存默认和命名 AsyncLogger，统一关闭 |

以下是后续目标数据流，尚未接通。批次回调和输出对象由长期 AsyncLogger 管理。

```text
DLOG_LOGGER_INFO(app) 或迁移后的 DLOG_INFO()
  → 长期 AsyncLogger 创建内部 LogMessage
  → LogMessage 通过 LogStream 格式化，析构时提交给 AsyncLogger
  → AsyncWorker
  → 批次输出回调
  → StdoutFlush、FileFlush 或 RollFileFlush
```

每个异步 AsyncLogger 持有一个 Worker。现有 FlexibleBuffer、LargeFixedBuffer、跨块追加和文件轮转继续复用。

内部 LogMessage 接替的是当前临时 Logger 的位置，不再额外包一层新的临时对象。每个流式表达式独享 LogStream；长期 AsyncLogger 不保存供所有生产线程共同拼接的 LogStream。格式化在业务线程完成，记录正常结束时调用一次 append，后台只接收字节并按批次输出，不访问业务对象或重新格式化。

## 5. 实施阶段与逐步审核

### 第一步：独立 AsyncWorker

已完成 `src/core/asyncworker.h/.cpp` 和直接测试。原 AsyncLogging、测试和异步示例目前保留原调用链。Worker 复用原后端的固定块组织和消费流程，输出由批次回调决定。旧调用链的迁移随长期 AsyncLogger 实施。

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
3. 线程生命周期随 AsyncLogger 接入处理。保留 Worker 的显式 start/stop，由后续长期 AsyncLogger 内部管理启动和关闭，业务侧不重复操作 Worker；暂不改为构造自动启动。

继续保留固定块队列和线程局部的 buffersToWrite、replaceBuffer，无需改成 Kama 的两个成员缓冲区。回调已使用 const LargeFixedBuffer &，缓冲区清空和复用由 Worker 管理；正常排空、重复停止安全及后台异常传递也继续保留。上述对比来自静态阅读，未运行 Kama，消费策略变更的吞吐影响尚未测量。

**验证范围。** Worker 测试覆盖输入复制、跨块和并发追加、周期处理、重复启停、正常排空、终端和文件回调，以及回调主动抛错后的异常传递。线程创建失败的回滚、Linux、持续压力和真实存储故障尚未验证。

加入异常用例后，Windows 使用 MSVC 直接构建并运行测试。静态链接和 DLL 两种构建均为 228 项通过、1 项因 MSVC 不支持负 time_t 而跳过。

### 第二步：统一 Logger（后续实施）

本节保留原阶段标题，便于已有链接继续定位；下面的“步骤 1～4”是本次确认的实施和审核单元。前面的独立 AsyncWorker 已完成，不计入这六个后续步骤。步骤 1 已实现并复核，步骤 2 中的 2.1、2.2 已实现，其他小步及步骤 3～6 待实施；不能因本文列出接口或验收条件而视为完成。

#### 已确认的设计

现有 `Logger` 是单条记录的临时对象，构造时保存时间、等级、源文件和行号，用 LogStream / FlexibleBuffer 拼接正文，析构时补换行并调用全局输出。按本次命名调整，新增长期 `AsyncLogger`，通过同文件中的 AsyncLoggerBuilder 构建；现有 Logger 保留兼容，单条记录职责已委托给内部 LogMessage。后续同一个 AsyncLogger 接收多条记录，每条流式表达式各自使用一个 LogStream。

| 层次 | 职责 | 接口边界 |
| --- | --- | --- |
| 内部 LogMessage | 记录创建时绑定目标和格式配置，保存元数据、流式拼接、析构时补换行并提交一次 | 向流式表达式提供本条 LogStream |
| 公开 AsyncLogger | 创建记录，持有一个 Worker 和一个 LogFlush，管理启动和回收 | message 提供单条消息；析构自动回收，stop 可主动等待并反馈失败 |
| AsyncLoggerBuilder | 接收构建配置，创建并启动 AsyncLogger | BuildLoggerFlush 配置输出；Build 返回 shared_ptr<AsyncLogger>，不参与运行期写入 |
| LogStream | 沿用现有 operator<< 和 FlexibleBuffer | 只负责格式化和缓冲，不接管记录元数据或析构提交 |
| AsyncWorker | 复制字节、排队、后台交付、正常排空和后台异常传递 | append/start/stop，仅用于工作器控制 |
| LogFlush | 将收到的字节写到指定目标 | writeAndFlush；文件路径和轮转配置在对应输出对象中处理 |

**创建与配置。** 统一通过 AsyncLoggerBuilder 创建，由 Builder 调用 AsyncLogger 的非公开构造。采用参考实现的 BuildLoggerFlush<T>(args...) 和 Build() 用法，BuildLoggerFlush 内部调用现有 LogFlushFactory，不再并列提供 setOutput。第一版只接一个目标，第二次配置明确报错；多目标留到步骤 6。未指定输出时，Build 为本次构建选择默认 StdoutFlush，不修改 Builder 自身配置。AsyncLogger 接收配置快照，后续操作 Builder 不修改已有 AsyncLogger 的目标或格式配置；若多个实例共享同一个 LogFlush，其输出资源仍然共享，不承诺重新打开独立文件。默认终端启用颜色，文件使用纯文本；本阶段不额外添加颜色、周期、名称等 setter，不迁 SAFE / UNSAFE 或 Builder 继承体系。Build 启动 Worker 后返回，业务侧不再手工启动；线程启动完成不等于后台已处理数据。

**所有权。** LogMessage 持有目标 AsyncLogger 的 shared_ptr，直到提交结束；释放外部指针不会令尚存活的消息悬空，也不等于立即析构 AsyncLogger。AsyncLogger 持有 Worker，输出回调按值捕获 shared_ptr<LogFlush>，直接调用 writeAndFlush，不捕获 AsyncLogger 的强引用或依赖裸 this。最后一个持有者释放 AsyncLogger 时自动停止，Worker 排空并 join 后才能释放输出。LogMessage 本身作为临时对象存在，不要求单独堆分配。

**停止与错误。** 正常路径由 AsyncLogger 析构自动排空并 join，不要求每次手写收尾；析构不向外抛异常。额外保留 stop()，用于调用方主动等待并捕获已保存的失败，不另设 shutdown。stop 关闭接收入口，成功后重复调用不重复回收；失败后重复调用仍能反馈原异常。停止后不接收新记录或缓存等待重启，AsyncLogger 第一版不提供重启。普通停止前应结束生产线程和临时消息，启停控制由调用方串行执行；FATAL 的并发协调留到步骤 3。LogMessage 析构中的补换行、提交异常不得逃出：保存首个前台失败并作最小 stderr 诊断，stop 还要反馈该错误；后台异常沿用 Worker 的保存和重抛机制。诊断不得递归调用日志宏。operator<< 自身的异常仍可由调用方捕获，不承诺格式化或输出失败后日志完整。

**与 Kama 的接口区别。** Kama Builder 已有 BuildLoggerFlush 和 Build；Kama AsyncLogger 没有 message 或公开的 stop/shutdown，依靠成员 Worker 的析构停止。dlog 的 message 是为了保留流式表达式；stop 是为保留当前 Worker 的显式异常反馈，均为本项目的适配，不宣称参考实现已有。异步 FATAL 需要内部排空，但这本身不构成新增公共停止接口的理由。

**提交与格式。** 时间、等级、源位置、errno、换行及显式字节长度沿用现有实现；正常记录格式化一次、调用一次 append，不另建独立的消息序列化路径。记录创建时绑定目标及颜色。宏仍捕获调用位置，目标表达式和 savedErrno 参数只求值一次，空目标在入口检查。跨块记录的顺序由 Worker 保证，不承诺分配失败时整条提交具有回滚能力，也不自动重试可能已部分接收或写出的记录。

#### 步骤 1：提取内部 LogMessage，保留当前入口

- **做什么：** 将当前 Logger / LoggerImpl 的单条记录职责移入内部 LogMessage，继续使用原 LogStream 和 FlexibleBuffer。临时兼容入口复用同一实现，保留现有默认宏、直接构造及 SetOutput/SetFlush，使本步可独立构建和审阅；不复制两套格式化代码，不接入新的 Worker 链路。
- **协调事项：** 过渡链路在记录创建时绑定输出、刷新回调及颜色快照，避免构造和析构分读全局目标；该过渡连接在步骤 4 清理，不作为新的公共回调扩展接口。检查 LogColorGuard 对 LogLevel/Logger 头文件的依赖，避免循环包含。
- **涉及：** logger.*、内部记录声明和实现、logger_test.cpp，以及必要的类型依赖；内部记录的具体文件布局按现有结构确定，不新增并行格式化框架。
- **验收：** 原有宏和直接构造仍可用；表达式结束时恰好提交一次；时间/等级/位置/errno/颜色/换行保持；空字节、5000 字节及跨多个块的长记录不截断；已有 FATAL 终止行为不回退。加入回调切换与记录绑定的验证，重新构建并运行受影响的核心测试。
- **审核点：** 只迁移单条记录职责，LogStream 仍可独立使用。报告结果后停止，等用户审核通过再开始步骤 2。

**本步实施结果（2026-11-10，复核通过）。** 新增 src/core/logmessage.h/.cpp，内部类型为 dlog::LogMessage，内部用途在说明和注释中表达；现有 logger.h/.cpp 文件和 Logger 类型保留，构造时绑定回调快照，stream 与析构提交复用同一份记录实现。FileNameView、LogLevel 和 LogLevelColorMode 随记录声明迁入 logmessage.h，原 logger.h 通过包含继续提供这些类型；LogColorGuard 的实现直接依赖记录声明，避免反向依赖兼容 Logger。现有宏、直接构造、SetOutput/SetFlush 和旧 AsyncLogging 调用链继续可用；对象布局已变化，使用方需重新编译。未增加长期 AsyncLogger、Builder 或新的 Worker 调用链。

新增 7 项测试，覆盖内部记录的长二进制消息和单次提交、空回调拒绝、原目标及颜色绑定、FATAL 刷新回调绑定，以及输出/刷新回调主动抛错。记录析构捕获失败并直接向 stderr 诊断，FATAL 即使输出失败仍尝试刷新后 abort；当前没有长期 AsyncLogger 的关闭接口，不提供关闭时查询或重抛错误，留到步骤 2。回调快照只复制 std::function，引用捕获资源仍由调用方保证有效。

Windows / MSVC / Debug / DLL：库构建成功，核心 140 项全部通过，包含既有跨多个异步缓冲块的长记录落盘用例。本轮复核运行 xmake test -y：共 238 项，237 项通过、1 项因 MSVC 不支持负 time_t 而跳过，无失败；数量按各组 GTest 的实际结果统计，未采用将跳过项误计为通过的脚本汇总行。LoggerTest 和 AsyncLoggingTest 两个原有示例均构建并运行成功。本轮未验证 Linux、静态链接、持续压力和真实存储故障。

复核按“多个生产线程、配置在开始产生日志前完成、禁止并发修改全局回调”的现有约定检查调用方、格式化、回调快照、析构异常、FATAL 和导出头文件依赖，未发现阻碍步骤 1 提交的问题。按用户要求分四批：独立 LogMessage 与共用类型及测试、Logger 委托与回归测试、README 使用说明、迁移计划与复核记录。第一批暂存内容另导出到独立目录，显式指定项目路径后完成配置、构建和核心测试，138 项全部通过；确认拆分不依赖第二批改动才能构建。继续停在步骤 1，待用户指示后才开始步骤 2。

#### 步骤 2：长期 AsyncLogger / AsyncLoggerBuilder 与单目标闭环

目标是将第一步的 LogMessage、现有 Worker 和输出目标连接起来，先完成显式单目标使用。拆分是为了逐项审阅，不要求每个小步都提前实现后续能力；每个小步都应可编译，只测试本步已实现行为，不加入额外的公共接口来绕过尚未接通的链路。

**接口清单。** BuildLoggerFlush、Build 和正常 stop 已实现；message 与宏仍待实现。AsyncLogger 和 AsyncLoggerBuilder 同置于 src/core/asynclogger.h，非模板实现放在对应 cpp 中。

| 接口 | 用途及来源 | 接入小步 |
| --- | --- | --- |
| AsyncLoggerBuilder::BuildLoggerFlush<T>(args...) | 沿用 Kama 的构建方式，内部复用 LogFlushFactory；本步只允许一个输出目标 | 2.1 |
| AsyncLoggerBuilder::Build() → shared_ptr<AsyncLogger> | 创建长期 AsyncLogger 并启动 Worker；无配置时使用终端 | 2.2 |
| AsyncLogger::stop() | dlog 的扩展；主动停止、排空并反馈异常，正常使用可省略，不提供重启 | 2.2 先实现正常停止，2.5 补齐错误反馈 |
| AsyncLogger::message(filename, line, level, savedErrno = 0) → LogMessage | 为流式调用适配的消息入口，供宏使用；本步仅支持非 FATAL 等级 | 2.3 |
| DLOG_LOGGER_TRACE/DEBUG/INFO/WARN/ERROR(app) | 显式指定目标，捕获调用位置 | 2.4 |
| DLOG_LOGGER_SYS_ERROR(app, savedErrno) | 传递调用方保存的 errno | 2.4 |

不增加 Logger::Create、setOutput、shutdown 或业务侧 start，也不增加 BuildLoggerName、BuildLopperType、颜色及周期配置、Builder 继承体系。message 是本项目拟定名称，签名如需调整，应在 2.3 的具体改动中说明理由并同步文档。内部提交和兼容连接不作为新增的稳定业务接口。

| 小步 | 本次交付 | 状态 |
| --- | --- | --- |
| 2.1 | Builder 的单输出配置 | 已完成，用户已指示继续 |
| 2.2 | 长期 AsyncLogger 的创建和自动回收 | 实现完成，待用户审核 |
| 2.3 | LogMessage 到 Worker 的正常提交 | 待实施 |
| 2.4 | 显式流式宏和系统错误入口 | 待实施 |
| 2.5 | 输出失败及前台提交失败的反馈 | 待实施 |
| 2.6 | 完整集成验收和使用说明 | 待实施 |

##### 2.1：Builder 单输出配置

- **做什么：** 实现 AsyncLoggerBuilder 的 BuildLoggerFlush<T>(args...)，转发参数给现有 LogFlushFactory 并保存输出对象。本阶段第二次配置明确报错；构造输出对象失败时不留下半成品配置。
- **范围：** 只处理构建配置，不改当前 Logger，不启动线程，不接入宏；Build 的实现留在 2.2，不添加只有声明却无实现的占位方法。输出路径、目录创建及轮转参数校验继续由 FileFlush / RollFileFlush 及底层负责，不再实现一份。
- **验证：** 普通文件和轮转输出参数能传入、无效路径或构造失败能反馈、重复配置会被拒绝；验证已有 Logger 和 LogMessage 仍能构建。Builder 的输出在 Build 之前就可能打开文件或创建目录，这与内部调用工厂的时点一致。
- **交付与审核：** 仅提交 Builder 配置及对应测试供用户审核；通过后再做 2.2。

**2.1 实施结果（2026-11-10，用户已指示继续 2.2）。** 本小步原新增 src/core/loggerbuilder.h（已在 2.2 合并到 asynclogger.h 并改名 AsyncLoggerBuilder），公开方法只有 BuildLoggerFlush<T>(args...)，通过现有 LogFlushFactory 创建并保存一个 shared_ptr<LogFlush>。第二次配置在创建输出对象前抛出 logic_error，因此不会额外创建目录或文件；工厂构造失败时不赋值，原异常直接传播，Builder 可继续尝试配置。2.1 交付时为纯头文件模板类，无需新增 cpp 或 DLL 导出符号，不添加 Build、getter、线程或默认输出占位逻辑；原 Logger / LogMessage 和构建配置未改动。

本小步原新增 test/core/loggerbuilder_test.cpp 共 4 项测试（已在 2.2 迁到 asynclogger_test.cpp），覆盖左值引用和不可复制参数转发、输出对象在 Builder 存活期间保留且析构时释放、带中文和 emoji 的普通及轮转路径、重复配置不产生额外文件，以及空路径/被文件占用的目录导致构造失败后仍能重试且保留原文件内容。Windows / MSVC / Debug / DLL 下，xmake build -P D:/Workspace/dlog -y dlog 成功，xmake test -P D:/Workspace/dlog -y tests/core 的 144 项全部通过。本轮未运行工具测试、Linux、静态链接、持续压力或真实存储故障验证；当时未开始 2.2，也未提交 Git。

##### 2.2：长期 AsyncLogger 的创建和自动回收

- **做什么：** 新增 asynclogger.h/.cpp，长期对象叫 AsyncLogger，同文件内的 Builder 改为 AsyncLoggerBuilder；加入 Build、单个 Worker、单个输出目标、析构自动回收，以及正常路径的 stop。Build 返回前启动 Worker；无输出配置时为本次构建准备 StdoutFlush。回调持有输出对象，Worker 退出后才能释放它；正常 stop 可重复，停止后不允许重启。
- **兼容迁移：** 本次通过 AsyncLogger 命名避免占用旧 Logger 名称，不提前迁移旧直接构造调用；现有 Logger、默认 DLOG_* 宏和 SetOutput/SetFlush 暂走旧回调链路，保留当前调用形式及原有 FATAL 行为。过渡连接仅服务旧入口，在步骤 4 清理，不维护两套格式化实现，不把默认宏提前改为异步。
- **验证：** 默认终端及显式文件目标可创建，空队列的 stop、重复 stop、自动析构回收正常，Builder 的后续操作不修改已建 Logger 的配置；验证可覆盖的创建失败清理。未能验证的线程创建失败路径如实列明。检查旧入口的构建及行为没有回退。
- **交付与审核：** 本小步尚未提供 AsyncLogger 的消息入口，不为测试额外公开裸字节写入接口。审核资源所有权、正常启停和兼容迁移，通过后再做 2.3。

**2.2 实施结果（2026-11-10，待审核）。** 新增 src/core/asynclogger.h/.cpp；AsyncLoggerBuilder 的完整声明与模板方法放在同一头文件，Build 的非模板实现放在对应 cpp，不再保留独立 loggerbuilder.h 或旧类名别名。长期对象改用 AsyncLogger，因此保留现有 Logger 文件、直接构造、静态回调与默认宏，无需本小步迁移旧调用方。

Build 返回 shared_ptr<AsyncLogger>，先完成对象与所有权建立，再启动一个 Worker；启动失败由局部 shared_ptr 析构回收。未配置输出时只为本次构建创建 StdoutFlush，不改变 Builder 配置。Worker 的回调按值持有 shared_ptr<LogFlush>，不捕获 AsyncLogger；最后一个日志器持有者释放时，由 Worker 析构停止并 join 后释放回调引用。Builder 若仍持有显式输出，输出对象会继续存活。可选 stop 直接复用 Worker 的排空、重复停止和后台异常重抛；不增加 start、shutdown、getter 或裸字节提交入口。消息提交和前台错误保存仍分别留在 2.3、2.5。

原 4 项配置测试迁至 test/core/asynclogger_test.cpp，并让普通及轮转文件测试实际 Build、stop。新增 3 项测试覆盖默认构建后再配置不改变既有对象、Builder 先释放后日志器仍持有输出、最后一个日志器引用释放时自动回收且没有循环持有，以及同一 Builder 多次构建生成不同日志器和 Worker、共享同一显式输出、重复停止及释放顺序。多次 Build 复用显式输出不等于创建独立文件；输出实现的并发写入要求随 2.3 接通时核验，本小步尚无消息入口。

Windows / MSVC / Debug / DLL：库构建成功，核心 147 项测试全部通过，包括旧 Logger、LogMessage 和 AsyncLogging 回归。新测试通过 DLL 链接并调用 Build、stop 和析构，验证当前导出可用；构建脚本按现有文件规则自动纳入新源码和头文件，无需修改清单。未运行工具测试、Linux、静态链接或安装后的外部消费验证，未注入线程创建及内存分配失败；当前仅验收空队列生命周期，实际带消息排空与输出失败仍待后续小步。本轮保留工作区已有的 LogMessage noexcept 声明改动；未提交 Git。

##### 2.3：接通消息提交与目标生命周期

- **做什么：** 实现 AsyncLogger::message，创建独享 LogStream 的 LogMessage，并在创建时绑定目标 AsyncLogger 及颜色；让消息正常析构时向 Worker 追加一次，后台通过目标的 writeAndFlush 写出。LogMessage 持有目标直至提交结束，输出回调不反向持有 AsyncLogger。
- **范围：** 先通过 message(...).stream() 验证正常日志，不接显式宏，不增加新的序列化路径、文件策略或 Worker 缓冲机制。终端默认启用等级颜色，文件使用纯文本；不新增颜色配置接口。保存失败并由 stop 反馈的完整链路在 2.5 验收，本小步不能宣称已经完成。
- **验证：** 短消息写完立即 stop 后可完整读取；最后一个持有者释放时自动排空；长正文、空字节和跨块内容保留；同一 AsyncLogger 的并发消息不交错；不同 AsyncLogger 不串写；外部 shared_ptr 释放后，尚未结束的消息仍能安全提交；停止后拒绝创建或提交新消息。
- **交付与审核：** 审核消息到 Worker 的调用链及目标生命周期，通过后再做 2.4。

##### 2.4：显式流式宏与 errno 入口

- **做什么：** 增加接口清单中的五个非 FATAL 等级宏和一个 SYS_ERROR 宏，复用同一个 message 入口，捕获调用处文件名及行号。
- **范围：** 目标表达式和 savedErrno 参数各求值一次，空目标在入口明确拒绝；不为每个等级重复实现格式化。默认 DLOG_INFO() 等宏仍保留旧兼容链路，不增加异步 FATAL / SYS_FATAL 宏。
- **验证：** 每个宏的等级、调用位置、正文及 savedErrno 正确；有副作用的参数只执行一次；空目标被拒绝；完整表达式结束时只提交一次。
- **交付与审核：** 审核实际调用写法和宏行为，通过后再做 2.5。

##### 2.5：停止和异常反馈

- **做什么：** 补齐前台补换行或提交失败的保存与诊断，并由 stop 反馈；后台输出失败沿用 Worker 保存原异常、停止消费和停止时重抛的机制。多个失败保留首个已记录的错误，不自动重试可能已经写出前缀的数据。
- **范围：** LogMessage 和 AsyncLogger 析构均不向外抛异常，普通使用仍可依赖自动回收；需要主动获取错误时才调用 stop。失败后仍要回收线程；重复 stop 不重复回收或输出，已保存错误仍可反馈；停止后拒绝使用。诊断直接写 stderr，不递归调用日志宏，也不新增错误回调注册接口。
- **验证：** 用可控的抛错输出目标覆盖后台失败，检查 stop 收到原异常；覆盖能可靠触发的前台提交失败和析构诊断；验证失败后的自动回收、重复 stop 及停止后拒绝。未覆盖的分配失败或真实存储故障必须单独列出，不把回调主动抛错当作真实磁盘故障验证。
- **交付与审核：** 审核错误是否可观察、回收是否完成，通过后再做 2.6。

##### 2.6：单目标集成验收和文档

- **做什么：** 结合前五个小步，完成显式文件示例和 README。展示 BuildLoggerFlush / Build / 显式宏的正常用法，说明最后一个持有者释放时自动回收；另列可选 stop 的主动等待及 try/catch 用法，不把它写成每次必做动作。
- **验证：** 普通文件、轮转文件和终端分别作为单目标运行；核对消息完整性、颜色、并发、目标独立、生命周期、停止和错误反馈；重新构建库及受影响示例，运行适用的核心与工具测试，检查新增公共类所需的 DLL 导出和头文件依赖。发布全流程仍留在步骤 6，不因此提前扩展多输出等功能。
- **交付与审核：** 报告当前真实接口、旧直接构造调用的迁移办法及重新编译要求，区分本轮实测和仍未验证的平台、压力或故障场景。完成后等待用户审核整个第二步，不自动开始步骤 3，也不自动提交 Git。

**每小步共同约定：** 当前 2.2 实现及本步验证完成，等待用户审核；2.3～2.6 均未实施。每次先说明要改哪些文件和行为，完成后报告实际验证及限制；用户明确审核通过后才进入下一小步。小步编号用于实施和审核，不强制每小步对应一次 Git 提交，提交仍由用户另行授权。

#### 步骤 3：显式 AsyncLogger 的 FATAL 排空

- **做什么：** 接入显式 FATAL / SYS_FATAL：提交致命记录，关闭该 AsyncLogger 的接收入口，排空此前已接收数据及致命记录，join 后 abort。协调仍在运行的生产者和多个关闭请求，避免继续追加、重复 join 或 join 自身；等待线程退出时不持有提交所需的锁。不依赖普通退出前已结束生产者的前提来实现 FATAL。
- **协调事项：** 当前 SetFlush 默认只刷新 stdout，不能复用为异步排空；writeAndFlush 的零长度调用也不承担排空。后台或提交失败时先作 stderr 诊断，再终止，不让异常逃出析构，也不宣称失败后仍可完整写盘。只保证目标 AsyncLogger 的已接收数据，不承诺排空其他 AsyncLogger。
- **涉及：** AsyncLogger 关闭协调、记录 FATAL 路径、显式宏和子进程集成测试。
- **验收：** 子进程终止后文件包含此前普通记录及致命记录；savedErrno 保留；输出失败仍有诊断并终止；有并发生产者或关闭请求时无死锁、无重复回收。不能只用“进程退出了”的断言替代文件检查。
- **审核点：** 用户确认 FATAL 语义与子进程证据后，再进入默认入口迁移。

#### 步骤 4：默认入口迁移和旧链路清理

- **做什么：** 保留 DLOG_INFO() 等调用形式，改为从默认 AsyncLogger 创建 LogMessage；接入已验收的 FATAL 路径和默认目标切换。默认目标为异步终端，AsyncLogger 仍自动管理生命周期；需要主动等待或获取异常时使用 stop，全局 Shutdown 在步骤 5 收口。
- **协调事项：** 退役 SetOutput/SetFlush 和旧临时 Logger 构造入口，自定义输出通过 LogFlush 接入。迁移现有异步示例、README 及测试后，移除旧 AsyncLogging 的重复队列/线程实现和临时兼容连接；不保留两个长期并行的后端。旧接口删除属于源代码和 ABI 变化，提供对应的新用法，并要求重新编译。
- **默认目标与延迟：** 生产线程启动前完成配置，不承诺并发热切换。记录创建时绑定目标；切换本身不提前关闭旧目标，旧目标由持有方在记录结束后关闭。默认终端迁移前必须明确未满块的延迟：若本步采用“有数据即可处理”，同时修改 append 通知和后台谓词，并验证顺序、停止与失败；若保留周期策略，则在公开用法中明确延迟，不能宣称日志立即显示。默认终端首次使用的初始化失败也须有明确反馈。
- **涉及：** 默认入口、logger.*、旧 asynclogging.* 及相关调用方、snippet、README、计划文档和测试；核对清理后的构建及导出。
- **验收：** 默认六级与系统错误宏可用；有序切换目标时未结束记录仍交给原目标且颜色正确；默认终端正常退出排空；新异步示例可运行；旧入口在源码和公开文档中的遗留已清理；若调整 Worker 唤醒则覆盖未满块及时处理。运行受影响的核心、工具和集成测试。
- **审核点：** 用户审核默认行为变化、旧接口迁移和清理结果后，再增加管理功能。

### 第三步：简单命名管理与多个输出目标

#### 步骤 5：简单命名管理与统一关闭

- **做什么：** 小型管理器保存默认和命名 AsyncLogger；在 Builder 中增加实际需要的名称配置，并提供明确注册和查询入口。重复名称拒绝，查不到返回空指针，显式日志入口仍检查空目标。配置和默认切换在业务线程启动前完成。
- **统一关闭：** Shutdown 关闭默认及已注册的日志器，对同一实例去重；一个实例失败仍尝试关闭其他实例，完成回收后反馈失败。未注册的显式 AsyncLogger 由调用方关闭。重复 Shutdown 安全，关闭后不自动重建默认 AsyncLogger，也不支持全局重新初始化。
- **涉及：** 管理器、Builder 的名称配置、默认入口、README 和命名/关闭集成测试。
- **验收：** 名称唯一、查询及空目标行为明确；不同日志器内容独立；默认与命名指向同一实例时不重复处理；有失败仍回收其他实例；重复关闭和退出时无悬空回调。
- **审核点：** 审核命名与全局生命周期，尚不增加多输出列表。

#### 步骤 6：多个输出目标与发布验证

- **做什么：** 输出目标扩为列表，将第二步 BuildLoggerFlush 的单目标限制扩展为追加目标，继续沿用该构建入口及同一个 Worker；后台按顺序交付每个目标，同批一个目标失败仍尝试其他目标，再反馈错误。Worker 保存异常后停止消费并放弃剩余批次的行为不变，不增加自动重试。慢目标会拖慢该 AsyncLogger。
- **格式与边界：** 混合终端与文件统一纯文本；只有终端输出时保留颜色。每目标独立格式、等级过滤、共享线程池、JSON 和网络备份仍不加入。
- **涉及：** Builder、AsyncLogger 批次回调、输出集成测试、README、snippet，以及 DLL 导出和安装后的公共头文件验证。
- **验收：** 正常情况下同一字节批次恰好交给每个配置目标一次，文件无 ANSI；同批一个目标失败仍尝试其余目标，关闭能反馈错误；完整构建和适用的全量测试通过；安装后的外部示例能仅通过公开头文件及库使用新接口。不能用结构检查替代行为验证，未运行的平台和压力场景继续列明。
- **审核点：** 报告最后一批实现和验证，等待用户审核；通过后才将对应待办标记为完成。

**每步交付统一要求：** 说明做了什么、为何这样改、实际运行哪些验证、哪些没有验证以及接口迁移风险；同步本文状态但区分“实现完成待审核”和“用户审核通过”。若发现必须超出当前步骤的接口或行为变化，先整理可审阅的具体调整，不自行跳到后续步骤。

## 6. 本轮边界与后续事项

- 自动回收或主动停止前先结束日志生产者及临时消息；AsyncLogger 最后一个持有者释放时自动排空，stop 用于主动等待及错误反馈。步骤 5 的管理器统一关闭用于确定应用退出时点，不要求每个普通 AsyncLogger 都手工关闭。第一版不承诺运行中并发切换配置或全局关闭后重新初始化。
- 完成接收、完成 fflush、完成系统存储同步是不同阶段。本轮只保证文件层刷新，不承诺断电持久性，也不直接迁 Kama 的 fsync 配置。
- 待写队列仍无总量上限。核心用法完成后再单独定义积压上限和满载策略，用可控慢输出测试超大消息、并发、停止和失败，不照搬可能永久等待的 SAFE 模式。
- 现有 LogFile 允许空文件接收超限批次；跨块记录可能跨文件，不承诺文件大小硬上限或按行轮转。
- 性能优化等测量后再做；比较前对齐消息量、线程数和刷新语义，同时核对实际字节、记录数、峰值内存与停止耗时。
- 只引入创建 AsyncLogger 所需的小型 Builder；同步框架、共享线程池、Builder 继承体系、JSON 配置、最低等级过滤和远程备份不列入本轮。

## 7. 阅读顺序与下一步

| 顺序 | Kama 参考 | dlog 对照 | 重点 |
| --- | --- | --- | --- |
| 1 | [LogFlush.hpp][ref-flush] | [LogFile](../src/core/logfile.h)、[FileUtil](../src/utils/fileutil.h) | 输出接口、所有权、刷新和失败反馈 |
| 2 | [AsyncWorker.hpp][ref-worker]、[AsyncBuffer.hpp][ref-buffer] | [AsyncWorker](../src/core/asyncworker.cpp)、[现有 AsyncLogging](../src/core/asynclogging.cpp)、[FixedBuffer](../src/utils/fixedbuffer.h) | 复制、交换、锁、唤醒与停止排空 |
| 3 | [AsyncLogger.hpp][ref-logger] | [AsyncLogger](../src/core/asynclogger.h)、[LogMessage](../src/core/logmessage.h)、[LogStream](../src/core/logstream.h) | 长期对象如何接收临时记录 |
| 4 | [Manager.hpp][ref-manager]、[MyLog.hpp][ref-entry]、[实际示例][ref-example] | [现有异步示例](../snippet/AsyncLoggingTest/main.cpp) | 默认入口、命名管理和生命周期 |

**独立 AsyncWorker 已完成；步骤 1 的内部 LogMessage 已实现、复核并按用户授权分批提交。** 当前 2.1、2.2 已实现并通过核心验证；AsyncLogger 与 AsyncLoggerBuilder 同置 asynclogger 文件，现有 Logger 文件与入口保留。2.2 等待用户审核，下一项为 2.3 的消息提交，只有用户指示继续后才开始。仅在用户明确要求时提交 Git，按功能组织相关实现、调用方、测试和文档，提交说明不用类型前缀。

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
