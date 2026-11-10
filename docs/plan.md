# dlog 与 Kama 对比及简化迁移计划

## 1. 目标与进度

更新日期：2026-11-10；本轮改动前代码基线：`db911ec`。本文合并原对比文档与迁移计划，后续只维护这一份。

借鉴 Kama 的长期日志器用法。dlog 后续统一公开 `Logger`，由它管理工作器和输出目标。业务侧继续使用流式宏，单条记录由内部对象负责。

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
- [x] 独立 AsyncWorker 完成正常路径的缓冲、后台消费、回调输出及停止排空；旧调用链暂保留。
- [x] Worker 捕获后台异常，显式停止时重抛原异常。
- [x] FileUtil 报告流写入失败，旧 AsyncLogging 保留短写诊断和刷新已接收数据的行为。
- [ ] 统一公开 Logger，先实现显式单目标写入、关闭排空和失败反馈。
- [ ] 验证 FATAL 排空，再迁移默认入口和旧调用链。
- [ ] 简单命名管理和多个输出目标。

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
| 日志器含义 | 临时 Logger 构造一条记录 | 长期 AsyncLogger 接收多条记录 | 公开 Logger 负责长期资源，内部 LogRecord 复用现有记录格式化 |
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

以下是默认入口迁移后的用法草案。第一批实现先验证显式文件日志器，见第五节。

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

LogFile 支持单参数普通模式、双参数轮转模式。输出层使用 FileFlush(filename) 或 RollFileFlush(basePath, rollSize)，后者继承前者，共用 writeAndFlush，不重复实现轮转。旧 AsyncLogging 直接使用 LogFile，在追加和刷新之间处理流写入错误。FileFlush 的双参数构造为 protected；直接调用旧入口的使用方需改用 RollFileFlush。AsyncLogging 路径接口及对象布局已改变，使用方需重新编译。普通模式重开时追加已有内容，LogFile 的 `rollFile()` 抛出 `std::logic_error`；两种模式都会自动创建缺失的父目录；目录创建失败或路径被非目录占用时抛出 filesystem_error。长期 Logger 的创建接口仍待实施，已有 LogFlushFactory 只创建输出对象。

LogFile 分别提供 `append()` 和 `flush()`。FileFlush 将两次调用组合成 `writeAndFlush()`。旧 AsyncLogging 保留定时等待，每个非空缓冲块作为一个输出批次，并显式刷新。直接使用 LogFile 的调用方需显式刷新，原三参数构造需改为两参数并重新编译。

默认终端从同步改为异步后，调用返回不代表终端已经显示内容；正常退出需要排空。

## 4. 组件分工

| 组件 | 职责与迁移方式 |
| --- | --- |
| 内部 LogRecord（待实现） | 承接现有临时 Logger 的职责，复用 LogStream / FlexibleBuffer，完成后提交一次 |
| 长期 Logger（待实现） | 持有工作器与输出目标，提供业务日志入口和显式关闭 |
| AsyncWorker | 负责字节复制、缓冲队列、周期处理和正常排空；通过回调交付批次，显式停止时报告后台异常 |
| 现有 AsyncLogging | 目前保留原文件后端和调用链，后续迁移到长期 Logger |
| LogFlush | 单一接口 `writeAndFlush(data, len)`：按显式长度写入并刷新；长度为 0 时直接返回 |
| StdoutFlush / FileFlush / RollFileFlush | 分别输出到终端、普通文件、轮转文件；文件类共用写入与刷新，轮转行为由 LogFile 决定 |
| 简单管理器 | 保存默认和命名 Logger，统一关闭 |

以下是后续目标数据流，尚未接通。批次回调和输出对象由长期 Logger 管理。

```text
DLOG_LOGGER_INFO(app) 或迁移后的 DLOG_INFO()
  → 长期 Logger 创建内部 LogRecord
  → LogRecord 通过 LogStream 格式化，析构时提交给 Logger
  → AsyncWorker
  → 批次输出回调
  → StdoutFlush、FileFlush 或 RollFileFlush
```

每个异步 Logger 持有一个 Worker。现有 FlexibleBuffer、LargeFixedBuffer、跨块追加和文件轮转继续复用。

## 5. 分三步实施

### 第一步：独立 AsyncWorker

已完成 `src/core/asyncworker.h/.cpp` 和直接测试。原 AsyncLogging、测试和异步示例目前保留原调用链。Worker 复用原后端的固定块组织和消费流程，输出由批次回调决定。旧调用链的迁移随长期 Logger 实施。

**与 Kama 的对应关系。** [Kama Worker][ref-worker] 接收字节、管理缓冲和线程，再把批次交给回调；[Kama AsyncLogger][ref-logger] 持有输出目标并提供日志业务入口。dlog Worker 采用同一职责边界，保留现有 LargeFixedBuffer 块队列、整条记录持锁追加、跨块顺序、锁外输出和备用块复用，不移植 SAFE / UNSAFE 模式或双 vector 缓冲。Kama Worker 在构造时启动；本轮保留已审阅的显式 start/stop，后续由长期日志器内部管理，业务层不再重复暴露线程启停。参考源码仅作静态阅读，未运行 Kama 验证。

**已实现接口。**

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
};
```

| 接口 | 当前语义 |
| --- | --- |
| 构造 | 拒绝空回调，准备生产者缓冲，不启动线程；周期间隔单位为秒，0 在等待时按 1 秒处理 |
| append | 复制字节，可在首次启动前或正常停止后追加；空指针或零长度为空操作；后台失败后拒绝非空追加并重抛原异常 |
| start | 由调用方串行控制；运行中重复调用无效，正常停止后可重启；线程启动失败时回滚；已有后台异常时重抛 |
| stop | 未启动时不输出已缓存数据；调用前应结束生产者。正常停止时排空并 join；后台失败时先 join，再重抛原异常 |
| 析构 | 停止并回收线程，后台异常由显式 stop 报告 |

OutputFunc 在后台释放队列锁后执行，数据仅在本次回调期间有效；异步转交其他线程必须自行复制。每个非空块为一个批次；输出目标和回调捕获对象必须活到 Worker stop/join 完成之后。不得从回调中启停或销毁 Worker，避免线程 join 自身。控制操作串行执行，多个生产者可并发 append。

**当前状态与待设计事项。**

- 成员包括输出回调、周期间隔、运行标志、Thread、互斥锁、条件变量、生产者缓冲和待处理块队列。后台本轮消费的块及备用块留在线程局部变量中。
- 完整块入队后通知，未满数据按周期处理。停止状态的更新与等待谓词使用同一把锁，避免遗漏唤醒；正常退出要求停止请求已到、待处理队列和生产者尾部均为空。
- 后台入口保存原异常，停止消费并放弃剩余待写数据。失败前已输出的字节无法回滚；失败状态保留到 Worker 销毁。
- 前台轮换缓冲时先准备替换块，后台缺少备用块时也先分配再移交，避免分配失败后丢失生产者缓冲指针。
- FileUtil 遇到流写入错误时保留已接收的字节数，并抛出带错误码的异常。FileFlush 将异常传给 Worker。旧 AsyncLogging 在追加处捕获这类错误，打印后继续刷新已接收的前缀。
- 不移植 Kama 的容量等待：超出固定容量的消息可能一直等待，停止条件也需单独处理；不采用其单次扩容策略。停止及排空采用 dlog 当前规则。

**验证范围。** Worker 测试覆盖输入复制、跨块和并发追加、周期处理、重复启停、正常排空、终端和文件回调，以及回调主动抛错后的异常传递。线程创建失败的回滚、Linux、持续压力和真实存储故障尚未验证。

加入异常用例后，Windows 使用 MSVC 直接构建并运行测试。静态链接和 DLL 两种构建均为 228 项通过、1 项因 MSVC 不支持负 time_t 而跳过。

### 第二步：统一 Logger（后续实施）

现有 `Logger` 是单条记录的临时对象，构造时保存时间、等级、源文件和行号，用 LogStream / FlexibleBuffer 拼接正文，析构时补换行并调用全局输出。FATAL 随后调用全局刷新并 abort。

Kama 的 AsyncLogger 提供日志入口并持有 Worker 和输出目标，宏补充调用位置。dlog 借鉴这一分工，公开名称统一为 `Logger`，现有临时对象的职责移入内部 `LogRecord`。同一个 Logger 可以接收多条记录，每条流式表达式各自使用一个 LogStream。

| 层次 | 职责 | 接口边界 |
| --- | --- | --- |
| 内部 LogRecord | 记录创建时绑定目标和格式配置，保存元数据、流式拼接、析构时补换行并提交一次 | 向流式表达式提供本条 LogStream |
| 公开 Logger | 创建记录，持有一个 Worker 和一个 LogFlush，管理启动和关闭 | 提供日志入口和显式关闭，关闭时排空并报告失败 |
| AsyncWorker | 复制字节、排队、后台交付、正常排空和后台异常传递 | append/start/stop，仅用于工作器控制 |
| LogFlush | 将收到的字节写到指定目标 | writeAndFlush；文件路径和轮转配置在对应输出对象中处理 |

公开构造方式、关闭函数的签名和兼容细节留到实现时确定。第一批只接一个输出目标，路径和轮转配置由具体输出对象管理。

**与现有代码协调时必须解决的问题。**

- 当前颜色在记录构造时读取全局配置，输出回调在记录析构时读取；后续应在记录创建时绑定目标及相应格式配置，避免记录存活期间切换目标造成错投或颜色不一致。
- 复用现有 LoggerImpl / LogStream 的时间、等级、源位置、errno 和换行处理，保证格式化一次、复制提交一次；保留流式宏、空字节及超长记录支持，不另造与其平行的序列化流程。
- 明确记录持有目标的方式。记录尚未析构时，目标资源必须有效；关闭顺序是结束生产者和临时记录、排空并 join Worker、释放输出。回调不得形成拥有自身的 shared_ptr 循环，也不能只捕获可能先析构的裸 this。
- 正常业务使用由长期日志器内部启动和回收 Worker。关闭是否提供独立入口、能否再次使用、错误如何查询均属于日志器语义，不以公开转发 start/stop 代替设计。
- FATAL 必须提交并排空目标后再 abort。当前 SetFlush 默认只刷新 stdout，不能直接当成异步排空；后台失败应有明确报告，异常不得从临时记录析构直接逃出。
- 当前 SetOutput/SetFlush 是进程全局回调，无法独立表达多个日志器。默认目标、显式目标及旧回调的保留范围要一起确定，禁止依赖业务线程反复改全局回调来选择日志器。

后续按以下顺序实施。示例名称仅用于说明预期用法。

1. 先接显式文件 Logger，例如 `DLOG_LOGGER_INFO(app) << "started"`。验证写一条短日志后立即关闭，文件内容完整。再让输出失败一次，验证显式关闭能拿到错误。
2. 接默认宏前验证 FATAL。提交致命记录后排空目标再 abort，用子进程检查文件中包含致命记录和此前已接收的记录。后台输出失败时先报告再终止。
3. 保留 `DLOG_INFO() << ...` 的调用形式，再迁移默认目标。核对直接构造 `Logger(__FILE__, ...)`、`Logger::SetOutput()` 和 `Logger::SetFlush()` 的调用方、示例及测试，确定它们的迁移方式。全局关闭应安全地重复执行，已关闭的日志器拒绝继续提交。

涉及：`src/core/logger.*`、确认后的长期日志器与记录接口、默认入口、README、异步示例及子进程集成测试。验收还应覆盖目标切换期间记录绑定、格式与颜色一致、临时记录存活时的资源生命周期，以及多个日志器互不串写。

### 第三步：简单命名管理与多个输出目标

- 小型管理器保存默认和命名 Logger，将第二步已有集合收口。
- 增加按名称查询。显式流式宏只求值一次目标表达式，仍在调用处捕获文件位置。
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
| 2 | [AsyncWorker.hpp][ref-worker]、[AsyncBuffer.hpp][ref-buffer] | [AsyncWorker](../src/core/asyncworker.cpp)、[现有 AsyncLogging](../src/core/asynclogging.cpp)、[FixedBuffer](../src/utils/fixedbuffer.h) | 复制、交换、锁、唤醒与停止排空 |
| 3 | [AsyncLogger.hpp][ref-logger] | [Logger](../src/core/logger.h)、[LogStream](../src/core/logstream.h) | 长期对象如何接收临时记录 |
| 4 | [Manager.hpp][ref-manager]、[MyLog.hpp][ref-entry]、[实际示例][ref-example] | [现有异步示例](../snippet/AsyncLoggingTest/main.cpp) | 默认入口、命名管理和生命周期 |

**独立 AsyncWorker 已完成正常排空和后台异常传递。后续从第五节的显式单目标 Logger 开始，先验收关闭排空和错误反馈。** 仅在用户明确要求时提交 Git，按功能组织相关实现、调用方、测试和文档，提交说明不用类型前缀。

参考链接是动态分支；上述对比继承此前源码阅读结论。未运行 Kama、远程备份服务、压力或存储故障对测，不能把静态分析当成运行验证。

[ref-flush]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/LogFlush.hpp
[ref-worker]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncWorker.hpp
[ref-buffer]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncBuffer.hpp
[ref-logger]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/AsyncLogger.hpp
[ref-manager]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/Manager.hpp
[ref-entry]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/logs_code/MyLog.hpp
[ref-example]: https://github.com/youngyangyang04/Kama-AsynLogSystem-CloudStorage/blob/main/log_system/examples/test.cpp
