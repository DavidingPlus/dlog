#include "reporter.h"

#include <iomanip>


bool DlogBenchReporter::ReportContext(const Context &)
{
    GetOutputStream() << "\n基准测试配置：每个场景 " << m_records << " 条日志，多线程场景 " << m_threads << " 个生产者。\n"
                      << "吞吐量通常越高越好，耗时和延迟通常越低越好；结果受构建模式及系统负载影响。\n";
#ifndef NDEBUG
    GetOutputStream() << "当前为 Debug 构建，适合检查功能；比较性能请使用 Release 构建。\n";
#endif


    return true;
}

void DlogBenchReporter::ReportRuns(const std::vector<Run> &runs)
{
    auto &out = GetOutputStream();
    for (auto &run : runs)
    {
        // 名称保留英文前缀供筛选；显示时从第一个中文字符开始。
        auto &name = run.run_name.function_name;
        auto chinese = name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789");
        out << "\n【" << (chinese == std::string::npos ? name : name.substr(chinese)) << "】";
        if (run.run_type == Run::RT_Aggregate)
        {
            if (run.aggregate_name == "mean")
            {
                out << " 平均值";
            }
            else if (run.aggregate_name == "median")
            {
                out << " 中位数";
            }
            else if (run.aggregate_name == "stddev")
            {
                out << " 标准差";
            }
            else if (run.aggregate_name == "cv")
            {
                out << " 变异系数";
            }
            else
            {
                out << " " << run.aggregate_name;
            }
        }
        else if (run.repetitions > 1 && run.repetition_index >= 0)
        {
            out << " 第 " << run.repetition_index + 1 << " / " << run.repetitions << " 次";
        }
        out << '\n';

        if (run.skipped != benchmark::internal::NotSkipped)
        {
            out << "  测试未完成：" << run.skip_message << '\n';
            continue;
        }

        // Google Benchmark 的变异系数是比例值，其他计数器保留原始单位。
        bool cv = run.run_type == Run::RT_Aggregate && run.aggregate_name == "cv";
        auto print = [&](const char *key, const char *label, double scale, const char *unit)
        {
            auto it = run.counters.find(key);

            if (it == run.counters.end()) return;

            // 耗时保留三位小数，避免微秒级排空时间被显示成 0.00 毫秒。
            int precision = cv ? 2 : (scale == 1000 || std::string(unit) == " 毫秒" ? 3 : 2);
            if (!cv && run.run_type == Run::RT_Iteration && (std::string(unit) == " 条" || std::string(unit) == " 字节" || std::string(unit) == " 次")) precision = 0;

            out << "  " << label << "：" << std::fixed << std::setprecision(precision)
                << it->second.value * (cv ? 100.0 : scale) << (cv ? "%" : unit) << '\n';
        };

        if (!cv && run.counters.count("total_records_per_second")) out << "  本批总耗时（含排空）：" << std::fixed << std::setprecision(3) << run.GetAdjustedRealTime() << " 毫秒\n";

        print("records", "日志记录", 1, " 条");
        print("records_per_logger", "每个日志器的记录", 1, " 条");
        print("output_bytes", "输出数据", 1, " 字节");
        print("submit_records_per_second", "前台提交速度", 1, " 条/秒");
        print("total_records_per_second", "含后台排空的速度", 1, " 条/秒");
        print("drain_seconds", "提交后等待排空", 1000, " 毫秒");
        print("batches", "后台输出批次", 1, " 次");
        print("samples", "延迟样本", 1, " 条");
        print("mean_ms", "平均回调延迟", 1, " 毫秒");
        print("p95_ms", "P95 回调延迟（95% 的样本不超过此值）", 1, " 毫秒");
        print("max_ms", "最大回调延迟", 1, " 毫秒");
        print("submit_seconds", "两种日志全部提交", 1000, " 毫秒");
        print("fast_seconds", "普通输出收到全部记录", 1000, " 毫秒");
        print("slow_seconds", "慢输出收到全部记录", 1000, " 毫秒");
        if (!cv && run.counters.count("records_per_logger")) out << "  两个日志器全部排空：" << std::setprecision(3) << run.GetAdjustedRealTime() << " 毫秒\n";
        print("fast_batches", "普通输出批次", 1, " 次");
        print("slow_batches", "慢输出批次", 1, " 次");
    }
}
