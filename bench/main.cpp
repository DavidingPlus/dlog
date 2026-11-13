#include "bench.h"
#include "fileutil.h"

#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif


int main(int argc, char const *argv[])
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

    std::filesystem::path directory;

    try
    {
        if (argc > 4) throw std::invalid_argument("用法：dlog-bench [记录数] [线程数] [重复次数]");

        // 可省略参数；指定时记录总数和生产者线程数必须为正整数。
        auto parsePositive = [](const char *text)
        {
            std::string value(text);
            if (value.empty() || std::string::npos != value.find_first_not_of("0123456789")) throw std::invalid_argument("记录数和线程数必须是正整数");

            unsigned long long result;
            try
            {
                result = std::stoull(value);
            }
            catch (const std::out_of_range &)
            {
                throw std::invalid_argument("记录数或线程数超出范围");
            }
            if (0 == result || result > static_cast<unsigned long long>(SIZE_MAX)) throw std::invalid_argument("记录数和线程数必须是范围内的正整数");
            return static_cast<size_t>(result);
        };

        size_t records = argc > 1 ? parsePositive(argv[1]) : 100000;
        size_t threads = argc > 2 ? parsePositive(argv[2]) : 4;
        size_t repetitions = argc > 3 ? parsePositive(argv[3]) : 5;

        // 每次运行创建新目录，遇到重名则尝试下一个编号，不覆盖旧文件。
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

        std::cout << "日志文件保存目录：" << directory.string() << '\n';

        auto &manager = dlog::LogManager::GetInstance();
        std::vector<std::shared_ptr<std::unique_ptr<dlog::FileFlush>>> fileOutputs;
        std::vector<std::filesystem::path> files;
        size_t caseIndex = 0;

        // 每种线程数分别测试只计数和真实写文件，两者使用不同日志器。
        for (size_t threadCount : {size_t{1}, threads})
        {
            for (bool fileCase : {false, true})
            {
                std::vector<double> submitRates, totalRates;
                for (size_t iteration = 0; iteration < repetitions; ++iteration)
                {
                    auto suffix = std::to_string(++caseIndex);
                    std::cout << "\n第 " << iteration + 1 << " / " << repetitions << " 次运行\n";

                    Rates rates;
                    if (!fileCase)
                    {
                        auto counts = std::make_shared<Counts>();
                        auto logger = manager.buildLogger<CountingFlush>("counting-" + suffix, counts);

                        rates = runCase("计数输出", logger, records, threadCount, counts);
                    }
                    else
                    {
                        auto fileName = "file-" + suffix;
                        auto file = directory / (fileName + ".log");
                        auto output = std::make_shared<std::unique_ptr<dlog::FileFlush>>();

                        fileOutputs.push_back(output);
                        files.push_back(file);

                        auto logger = manager.buildLogger<BenchFileFlush>(fileName, file, output);

                        rates = runCase("文件输出", logger, records, threadCount, {}, file);
                        // runCase 已停止并校验该 Logger，可以关闭文件而无需等 Manager 析构。
                        output->reset();
                    }

                    submitRates.push_back(rates.submit);
                    totalRates.push_back(rates.total);
                }

                std::cout << "\n"
                          << (fileCase ? "文件输出" : "计数输出") << "，线程数 " << threadCount
                          << "：提交吞吐量中位数=" << median(submitRates) << " 条/秒，总吞吐量中位数="
                          << median(totalRates) << " 条/秒\n";
            }
        }

        auto latency = std::make_shared<LatencyState>();
        auto latencyLogger = manager.buildLogger<LatencyFlush>("latency", latency);
        runLatency(latencyLogger, latency, 100);

        auto slowCounts = std::make_shared<Counts>();
        auto slowLogger = manager.buildLogger<CountingFlush>("slow-output", slowCounts, 1);
        runCase("慢输出（每批等待 1 毫秒）", slowLogger, records, threads, slowCounts);

        auto sharedSlowCounts = std::make_shared<Counts>();
        auto sharedFastCounts = std::make_shared<Counts>();
        sharedSlowCounts->expected = sharedFastCounts->expected = records;
        auto sharedSlow = manager.buildLogger<CountingFlush>("shared-slow", sharedSlowCounts, 1);
        auto sharedFast = manager.buildLogger<CountingFlush>("shared-fast", sharedFastCounts);
        runSharedPool(sharedSlow, sharedFast, sharedSlowCounts, sharedFastCounts, records);

        dlog::LogManager::Shutdown();

        // 停止后台任务后关闭文件，只删除本次创建的文件，不递归删除其他内容。
        for (auto &output : fileOutputs) output->reset();
        for (auto &file : files) std::filesystem::remove(file);
        if (!std::filesystem::remove(directory)) throw std::runtime_error("基准测试目录不存在或无法删除");

        std::cout << "\n基准测试文件已清理。\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << "基准测试失败：" << error.what() << '\n';
        if (!directory.empty()) std::cerr << "未清理的文件目录：" << directory.string() << '\n';
        return EXIT_FAILURE;
    }
}
