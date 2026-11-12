# Windows 业务 DLL 内的日志退出清理问题

状态：已提供可选的 `LogManager::Shutdown()`；Worker 已接入共享池，DLL 退出阶段的自动兜底仍是 TODO。

业务 DLL 应在正常业务退出或卸载前，由普通控制线程调用 `dlog::LogManager::Shutdown()`。调用前先停止外部日志生产者、线程池任务提交者并结束临时日志消息；调用会先等待线程池空闲，期间池中任务仍可提交后续任务和日志，随后停止并排空 Manager 管理的日志器，最后关闭线程池。Manager 在析构前仍持有已停止的 Logger。不要从 `DllMain`、线程池任务或输出回调中调用，也不要并发调用关闭接口。关闭后 Manager 不支持重新使用。

普通 EXE 场景仍可依赖正常析构自动清理；显式关闭是可选的。如果从未使用 Manager 就调用 `Shutdown()`，会先创建实例再关闭。后台输出失败时，关闭会继续停止其他日志器，最后抛出第一个异常。

## 1. 问题范围

本问题针对 Windows 上的以下场景：业务 DLL 内部调用 `LogManager::GetInstance()`，由该 DLL 使用自己的 Manager 和后台工作线程；主程序正常退出时，DLL 内尚有日志或任务没有处理完。

它不同于 EXE 直接调用 dlog 的情况。即使 dlog 本身编译为 DLL，只要 Manager 由 EXE 侧创建，就属于 EXE 侧的使用路径。当前 Windows / MSVC / x64 / Debug 测试中，EXE 侧已注册 Logger 的正常退出排空测试通过；这只是已测构建和路径的结果，不代表所有平台和退出方式都有保证。

EXE 与不同业务模块各自管理自己的 Manager 是预期的使用边界，本文不要求跨模块共享单例或线程池。

## 2. 当前实现与现象

- `LogManager::GetInstance()` 定义在 `src/core/logmanager.h`，局部静态对象在调用它的模块侧建立生命周期。将定义放在头文件，是当前 EXE 侧自动清理方案的一部分。
- 本次历史复现时，Logger 输出尚未接入共享线程池，每个 `LogWorker` 仍有自己的后台线程；当时分别检查线程池任务与 Logger 输出。当前 Worker 已接入共享池，原复现结论不能直接视为覆盖新实现。
- 正常关闭时，Manager 会先等待共享线程池空闲，再停止其管理的 Logger，最后关闭线程池；即使外部仍持有线程池的共享指针，关闭后的线程池也不再接收任务。
- 业务 DLL 侧创建 Manager 后，在测试中分别提交一条 Logger 日志和一个向文件写内容的线程池任务。直接正常退出时，本次测试没有读到尾部日志及任务输出；主动调用该 Logger 的 `stop()` 并等待该任务后，可以读到输出。

上述主动停止单个 Logger 和等待单个任务的历史对照，只证明该 Logger 和该任务能够完成。现在另有 `Shutdown()` 的显式关闭测试。日志已提前写完时也可能观察不到丢失，因此不应根据一次运行推断每次都会丢失。

## 3. 退出时序分析

Windows 正常结束进程时，`ExitProcess` 先终止其他线程，之后才通知 DLL 执行进程分离；DLL 内部注册的退出回调及 C++ 静态对象析构处于 DLL 退出过程。EXE 内退出回调的执行时机与之不同。

在本次临时诊断中，先确保线程池工作线程已经开始执行任务并等待信号，然后让进程正常退出：

1. DLL 的退出回调执行，并向等待中的任务发送信号。
2. 在该回调中等待 300 毫秒，任务仍未完成。
3. 临时标记显示 `LogManager` 析构函数体进入并执行到末尾，但日志文件仍为空。

这与上述系统退出时序一致：**Manager 的清理代码执行时，后台线程可能已经无法继续消费数据**。`join()` 返回只能说明线程结束，不能证明它正常排空了工作。临时诊断标记不是项目代码；该实验也没有证明所有退出时机都会得到相同结果。

此外，线程被终止时可能持有锁，后续等待同一把锁可能卡住。业务 DLL 在进程运行中被显式卸载也是不同的路径；本次不把正常进程退出的观察结果当作该路径已验证的结论。

## 4. 如何复现

测试位于 `test/core/logmanager_test.cpp`，DLL 探针复用 `test/core/logmanager_test_helper.cpp`，由 `test/xmake.lua` 的 `logmanager_dll_probe` 目标构建。测试通过独立子进程复现 DLL 内的调用，并设置超时，避免退出卡住时一直占用测试进程。

相关用例：

- `LogManagerExitFileDeathTest.NormalExitFlushesRegisteredLogger`：EXE 侧创建 Manager，正常退出时检查已注册 Logger 的文件输出；该路径此前通过。
- `LogManagerDllExitTest.ExplicitShutdown`：DLL 内主动调用 `Shutdown()`，卸载 DLL 后正常退出，检查日志和任务输出。

DLL 和静态库的原始复现只覆盖 Windows x64 Debug；其他平台、构建模式和退出时机尚未覆盖。未主动调用 `Shutdown()` 的自动清理问题仍待后续研究。

## 5. 后续研究方向

可研究 Quill 在后台线程未正常完成退出时，由执行停止操作的线程尝试排空剩余日志的做法。它是**尽力兜底**思路，不意味着线程被强制终止后一定能无损恢复。

在 dlog 中验证时需要特别考虑：

1. `LogWorker::threadFunc()` 的 `buffersToWrite` 是后台线程局部批次；线程被终止时，已经转移到局部变量的缓冲区未必还能由清理线程访问。
2. 后台线程可能在持锁或正在执行输出回调时被终止；清理线程不能假定锁一定可用或输出状态一定完整。重复写入可能造成重复日志。
3. `ThreadPool` 队列中的普通任务可以包含任意用户代码；已开始的任务不能安全地一律重跑，日志缓冲兜底与通用任务恢复需要分开考虑。
4. 共享池已接入日志输出，仍需验证异常退出时任务与 Worker 状态的所有权及关闭行为。
5. 保留 EXE 侧现有自动退出行为，验证部分缓冲区、正在输出、线程持锁及正常退出等边界；结果须区分已恢复、无法恢复和可能卡住的情况。

本 TODO 暂不确定具体方案，不承诺在强制结束、进程崩溃或任意线程终止时无损恢复日志。

## 6. 参考资料

- [Quill：DLL 中异步日志丢失的讨论](https://github.com/odygrd/quill/discussions/925)：与本次复现相近的退出时序及作者提出的兜底思路。
- [Quill v12.0.0 的 BackendWorker 实现](https://github.com/odygrd/quill/blob/v12.0.0/include/quill/backend/BackendWorker.h)：后台线程未完成正常退出时的尽力排空实现。
- [Microsoft：ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess)：进程退出时线程终止和 DLL 分离的顺序。
- [Microsoft：DLL 最佳实践](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)：DLL 退出阶段的线程同步和锁限制。
