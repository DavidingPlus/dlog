#include "bench.h"
#include "fileutil.h"

#include <iostream>
#include <stdexcept>

#include <benchmark/benchmark.h>

#ifdef _WIN32
#include <windows.h>
#endif


int main(int argc, char **argv)
{
#ifdef _WIN32
    // Windows 控制台使用 UTF-8 显示中文；退出时恢复原来的代码页。
    struct ConsoleCodePage
    {
        UINT previous = GetConsoleOutputCP();

        ConsoleCodePage()
        {
            if (previous) SetConsoleOutputCP(CP_UTF8);
        }

        ~ConsoleCodePage()
        {
            std::cout.flush();
            std::cerr.flush();
            if (previous) SetConsoleOutputCP(previous);
        }
    } consoleCodePage;
#endif

    size_t records = 100000;
    size_t threads = 4;

    try
    {
        // 先取出 dlog 专用参数并压紧 argv，剩余参数交给 Google Benchmark 解析。
        int kept = 1;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);
            size_t *target = nullptr;
            size_t offset = 0;
            if (arg.rfind("--dlog-records=", 0) == 0)
            {
                target = &records;
                offset = 15;
            }
            else if (arg.rfind("--dlog-threads=", 0) == 0)
            {
                target = &threads;
                offset = 15;
            }
            if (!target)
            {
                argv[kept++] = argv[i];
                continue;
            }

            // 两种参数前缀均为 15 字节，例如 --dlog-records=17 截取出 17。
            auto value = arg.substr(offset);
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) throw std::invalid_argument("记录数和线程数必须是正整数");

            auto parsed = std::stoull(value);
            if (!parsed || parsed > static_cast<unsigned long long>(SIZE_MAX)) throw std::invalid_argument("记录数和线程数必须是范围内的正整数");
            *target = static_cast<size_t>(parsed);
        }
        argc = kept;
        argv[argc] = nullptr;
    }
    catch (const std::exception &error)
    {
        std::cerr << "基准测试参数错误：" << error.what() << '\n';
        return EXIT_FAILURE;
    }

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return EXIT_FAILURE;

    // 测试失败时保留目录；仅在全部测试及关闭成功后删除本次创建的文件。
    std::filesystem::path directory;
    std::vector<std::filesystem::path> files;
    std::vector<std::shared_ptr<std::unique_ptr<dlog::FileFlush>>> outputs;
    size_t index = 0;
    bool failed = false;
    bool managerUsed = false;
    std::vector<std::unique_ptr<std::string>> errors;

    auto reportError = [&](benchmark::State &state, const std::exception &error)
    {
        failed = true;
        // 框架可能在回调返回后才读取错误信息，需要保持字符串有效。
        errors.push_back(std::make_unique<std::string>(error.what()));
        state.SkipWithError(errors.back()->c_str());
    };

    auto nextName = [&]
    { return std::to_string(++index); };

    // 只有文件场景真正运行时才创建目录，避免筛选其他场景也产生空目录。
    auto makeDirectory = [&]
    {
        if (!directory.empty()) return;
        auto stamp = Clock::now().time_since_epoch().count();
        for (size_t attempt = 0; attempt < 100; ++attempt)
        {
            auto candidate = dlog::FileUtil::executableDirectory() / ("dlog-bench-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(candidate))
            {
                directory = std::move(candidate);
                break;
            }
        }
        if (directory.empty()) throw std::runtime_error("无法创建不重名的基准测试目录");
        std::cerr << "日志文件保存目录：" << directory.string() << '\n';
    };

    // 每个迭代是完整批次；runCase 计时从生产者同时开始到 stop 排空，不含创建、校验和清理。
    auto registerThroughput = [&](const char *name, size_t threads, bool fileCase, unsigned delay)
    {
        // 注册函数返回后框架才执行回调，因此名称和场景参数必须按值捕获。
        benchmark::RegisterBenchmark(name, [&, name, threads, fileCase, delay](benchmark::State &state)
                                     {
            for (auto _ : state)
            {
                try
                {
                    managerUsed = true;
                    auto &manager = dlog::LogManager::GetInstance();
                    auto suffix = nextName();
                    std::shared_ptr<Counts> counts;
                    std::shared_ptr<std::unique_ptr<dlog::FileFlush>> output;
                    std::filesystem::path file;
                    dlog::Logger::SharedPtr logger;

                    if (fileCase)
                    {
                        makeDirectory();
                        file = directory / ("file-" + suffix + ".log");
                        files.push_back(file);
                        output = std::make_shared<std::unique_ptr<dlog::FileFlush>>();
                        outputs.push_back(output);
                        logger = manager.buildLogger<BenchFileFlush>("file-" + suffix, file, output);
                    }
                    else
                    {
                        counts = std::make_shared<Counts>();
                        logger = manager.buildLogger<CountingFlush>("counting-" + suffix, counts, delay);
                    }

                    auto result = runCase(name, logger, records, threads, counts, file);

                    if (output) output->reset();

                    // 框架的 Time 使用手动提供的总秒数；提交吞吐等单独放入计数器。
                    state.SetIterationTime(result.seconds);
                    state.counters["submit_records_per_second"] = result.submit;
                    state.counters["total_records_per_second"] = result.total;
                    state.counters["drain_seconds"] = result.drainSeconds;
                    state.counters["output_bytes"] = static_cast<double>(result.bytes);
                    state.counters["records"] = static_cast<double>(records);

                    if (counts) state.counters["batches"] = static_cast<double>(counts->batches);
                }
                catch (const std::exception &error) { reportError(state, error); }
            } })
            // 每轮固定一个批次，避免自动校准创建大量由 Manager 持有的 Logger。
            ->Iterations(1)
            ->UseManualTime()
            ->Unit(benchmark::kMillisecond);
    };

    registerThroughput("count_single_单线程计数输出", 1, false, 0);
    registerThroughput("file_single_单线程文件输出", 1, true, 0);
    registerThroughput("count_multi_多线程计数输出", threads, false, 0);
    registerThroughput("file_multi_多线程文件输出", threads, true, 0);
    registerThroughput("slow_multi_多线程慢输出", threads, false, 1);

    benchmark::RegisterBenchmark("latency_低频回调延迟", [&](benchmark::State &state)
                                 {
        for (auto _ : state)
        {
            try
            {
                managerUsed = true;
                auto data = std::make_shared<LatencyState>();
                auto logger = dlog::LogManager::GetInstance().buildLogger<LatencyFlush>("latency-" + nextName(), data);
                auto result = runLatency(logger, data, 100);

                // 低频场景主时间是单条平均延迟：毫秒换算为框架要求的秒。
                state.SetIterationTime(result.meanMs / 1000.0);
                state.counters["samples"] = 100;
                state.counters["mean_ms"] = result.meanMs;
                state.counters["p95_ms"] = result.p95Ms;
                state.counters["max_ms"] = result.maxMs;
            }
            catch (const std::exception &error) { reportError(state, error); }
        } })
        ->Iterations(1)
        ->UseManualTime()
        ->Unit(benchmark::kMillisecond);

    benchmark::RegisterBenchmark("shared_pool_共享线程池", [&](benchmark::State &state)
                                 {
        for (auto _ : state)
        {
            try
            {
                managerUsed = true;
                auto &manager = dlog::LogManager::GetInstance();
                auto slowCounts = std::make_shared<Counts>();
                auto fastCounts = std::make_shared<Counts>();
                slowCounts->expected = fastCounts->expected = records;
                auto suffix = nextName();
                auto slow = manager.buildLogger<CountingFlush>("slow-" + suffix, slowCounts, 1);
                auto fast = manager.buildLogger<CountingFlush>("fast-" + suffix, fastCounts);
                auto result = runSharedPool(slow, fast, slowCounts, fastCounts, records);

                state.SetIterationTime(result.totalSeconds);
                state.counters["records_per_logger"] = static_cast<double>(records);
                state.counters["submit_seconds"] = result.submitSeconds;
                state.counters["fast_seconds"] = result.fastSeconds;
                state.counters["slow_seconds"] = result.slowSeconds;
                state.counters["slow_batches"] = static_cast<double>(slowCounts->batches);
                state.counters["fast_batches"] = static_cast<double>(fastCounts->batches);
            }
            catch (const std::exception &error) { reportError(state, error); }
        } })
        ->Iterations(1)
        ->UseManualTime()
        ->Unit(benchmark::kMillisecond);

    try
    {
        benchmark::AddCustomContext("dlog_records", std::to_string(records));
        benchmark::AddCustomContext("dlog_threads", std::to_string(threads));

        std::cerr << "指标说明：Time 为手动计时，CPU 为框架测得的主线程 CPU 时间，Iterations 为迭代次数。\n"
                  << "submit_records_per_second / total_records_per_second：提交 / 排空后吞吐量（条/秒）；drain_seconds：提交后排空秒数。\n"
                  << "records / output_bytes / batches：记录数 / 字节数 / 输出批次数；samples：延迟样本数。\n"
                  << "mean_ms / p95_ms / max_ms：平均 / P95 / 最大延迟（毫秒）。\n"
                  << "submit_seconds / fast_seconds / slow_seconds：共享池提交 / 普通输出完成 / 慢输出完成的秒数。\n";

        benchmark::RunSpecifiedBenchmarks();

        // 先停止池中任务，再释放文件句柄，Windows 才能删除这些文件。
        if (managerUsed) dlog::LogManager::Shutdown();

        for (auto &output : outputs) output->reset();

        if (!failed && !directory.empty())
        {
            for (const auto &file : files) std::filesystem::remove(file);
            if (!std::filesystem::remove(directory)) throw std::runtime_error("无法删除基准测试目录");
            std::cerr << "基准测试文件已清理。\n";
        }
    }
    catch (const std::exception &error)
    {
        failed = true;
        std::cerr << "基准测试失败：" << error.what() << '\n';
    }

    if (failed && !directory.empty()) std::cerr << "未清理的文件目录：" << directory.string() << '\n';

    benchmark::Shutdown();


    // SkipWithError 标记框架结果；另外返回非零状态，供脚本和 CI 检测失败。
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
