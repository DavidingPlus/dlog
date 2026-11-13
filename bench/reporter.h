#ifndef _DLOG_BENCH_REPORTER_H_
#define _DLOG_BENCH_REPORTER_H_

#include <benchmark/benchmark.h>


// 只改变控制台展示；框架仍可使用自己的 JSON 报告器写文件。
class DlogBenchReporter : public benchmark::BenchmarkReporter
{

public:

    DlogBenchReporter(size_t records, size_t threads) : m_records(records), m_threads(threads) {}

    bool ReportContext(const Context &context) override;

    void ReportRuns(const std::vector<Run> &runs) override;


private:

    size_t m_records;

    size_t m_threads;
};


#endif
