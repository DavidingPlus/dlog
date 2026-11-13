#!/usr/bin/env python3
"""将一次 dlog 基准测试的 JSON 结果绘制为中文图片。"""

import argparse
import json
import sys
from pathlib import Path

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib import font_manager
except ImportError:
    sys.exit('缺少 matplotlib，请先运行：python -m pip install "matplotlib>=3.6"')


CASES = {
    "count_single": "单线程计数",
    "file_single": "单线程文件",
    "count_multi": "多线程计数",
    "file_multi": "多线程文件",
    "slow_multi": "多线程慢输出",
    "latency": "低频回调",
    "shared_pool": "共享线程池",
}


def load_runs(path):
    with path.open(encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict) or not isinstance(data.get("benchmarks"), list):
        raise ValueError("JSON 中缺少 benchmarks 数组")

    runs = {key: [] for key in CASES}
    seen = {key: 0 for key in CASES}
    for row in data["benchmarks"]:
        if not isinstance(row, dict) or row.get("run_type", "iteration") != "iteration":
            continue
        name = row.get("run_name", row.get("name", ""))
        if not isinstance(name, str):
            continue
        case = next((key for key in CASES if name.startswith(key + "_")), None)
        if case:
            row = dict(row)
            row.setdefault("repetition_index", seen[case])
            seen[case] += 1
        if case and not row.get("error_occurred", False):
            runs[case].append(row)
        elif case:
            print(
                f"跳过失败的场景：{CASES[case]}：{row.get('error_message', '未知错误')}", file=sys.stderr)
    return data.get("context", {}), runs


def choose_font():
    available = {font.name for font in font_manager.fontManager.ttflist}
    for name in ("Microsoft YaHei", "SimHei", "SimSun", "Noto Sans CJK SC", "Noto Sans CJK JP", "WenQuanYi Micro Hei", "PingFang SC", "Heiti SC"):
        if name in available:
            plt.rcParams["font.family"] = name
            plt.rcParams["axes.unicode_minus"] = False
            return
    raise ValueError("未找到支持中文的字体，请安装微软雅黑、黑体或 Noto Sans CJK 后重试")


def plot_panel(ax, groups, title, unit):
    """按原始轮次绘图，缺失轮次断线，避免把失败的轮次当成成功结果。"""
    last_round = max(round_number for _,
                     samples in groups for round_number, _ in samples)
    for label, samples in groups:
        by_round = dict(samples)
        rounds = range(1, last_round + 1)
        ax.plot(rounds, [by_round.get(number, float("nan")) for number in rounds],
                marker="o", markersize=5, linewidth=2, label=label)
    ax.set_title(title, loc="left", fontsize=12, fontweight="bold")
    ax.set_xlabel("测试轮次")
    ax.set_ylabel(unit)
    ax.set_xticks(range(1, last_round + 1))
    ax.grid(alpha=0.2)
    ax.legend(loc="upper left", bbox_to_anchor=(1.02, 1), frameon=False)


def values(rows, key, factor=1):
    result = []
    for index, row in enumerate(rows):
        scale = factor.get(row.get("time_unit")) if isinstance(
            factor, dict) else factor
        if isinstance(row.get(key), (int, float)) and scale is not None:
            result.append(
                (row.get("repetition_index", index) + 1, row[key] * scale))
    return sorted(result)


def groups_for_cases(runs, key, factor=1):
    return [(label, samples) for case, label in list(CASES.items())[:5]
            if (samples := values(runs[case], key, factor))]


def save_chart(directory, filename, title, panels, context, note=""):
    panels = [panel for panel in panels if panel[2]]
    if not panels:
        return False
    fig, axes = plt.subplots(len(panels), 1, figsize=(
        11, 3.8 * len(panels)), squeeze=False, layout="constrained")
    context = context if isinstance(context, dict) else {}
    date = context.get("date", "未知时间")
    records = context.get("dlog_records", "未知")
    threads = context.get("dlog_threads", "未知")
    fig.suptitle(f"{title}  |  {date}\n每场景记录数：{records}  |  多线程生产者：{threads}",
                 fontsize=13, fontweight="bold")
    for ax, (subtitle, unit, groups) in zip(axes.flat, panels):
        plot_panel(ax, groups, subtitle, unit)
    if note:
        fig.text(0.99, 0.005, note, ha="right", fontsize=9)
    output = directory / filename
    fig.savefig(output, dpi=160, bbox_inches="tight")
    plt.close(fig)
    print(f"已生成：{output}", file=sys.stderr)
    return True


def main():
    parser = argparse.ArgumentParser(
        description="将 dlog 的 Google Benchmark JSON 绘制为中文图片。")
    parser.add_argument("json_file", type=Path, help="基准结果 JSON 文件路径")
    args = parser.parse_args()

    try:
        context, runs = load_runs(args.json_file)
        choose_font()
        directory = args.json_file.resolve().parent
        throughput = [
            ("前台提交吞吐量", "条/秒", groups_for_cases(runs, "submit_records_per_second")),
            ("含后台排空的总吞吐量", "条/秒", groups_for_cases(runs, "total_records_per_second")),
        ]
        # real_time 是 Google Benchmark 手动计时结果，按 time_unit 转为毫秒。
        factors = {"ns": 1e-6, "us": 1e-3, "ms": 1, "s": 1000}
        total_groups = []
        for case, label in list(CASES.items())[:5]:
            samples = values(runs[case], "real_time", factors)
            if samples:
                total_groups.append((label, samples))
        durations = [
            ("本批总耗时（含排空）", "毫秒", total_groups),
            ("提交后排空耗时", "毫秒", groups_for_cases(runs, "drain_seconds", 1000)),
        ]
        latency = [("低频回调延迟", "毫秒", [(label, samples) for key, label in
                    (("mean_ms", "平均延迟"), ("p95_ms", "P95 延迟"), ("max_ms", "最大延迟"))
                    if (samples := values(runs["latency"], key))])]
        shared = [("从开始到各阶段完成", "毫秒", [(label, samples) for key, label in
                   (("submit_seconds", "全部提交"),
                    ("fast_seconds", "普通输出收全"), ("slow_seconds", "慢输出收全"))
                   if (samples := values(runs["shared_pool"], key, 1000))])]
        charts = [
            ("throughput.png", "吞吐量", throughput, ""),
            ("duration.png", "耗时", durations, "排空耗时已包含在总耗时中"),
            ("latency.png", "回调延迟", latency, "P95 为每轮独立计算的结果"),
            ("shared_pool.png", "共享线程池", shared, ""),
        ]
        print()
        generated = sum(save_chart(directory, filename, title, panels, context, note)
                        for filename, title, panels, note in charts)
        if not generated:
            raise ValueError("没有可绘制的逐轮结果；请检查筛选条件及是否只导出了汇总结果")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"绘图失败：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
