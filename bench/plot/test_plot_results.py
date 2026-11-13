import json
import math
import tempfile
import unittest
from pathlib import Path

import plot_results


class PlotResultsTest(unittest.TestCase):
    def test_loads_only_successful_iterations(self):
        data = {"context": {"dlog_records": "10"}, "benchmarks": [
            {"name": "count_single_单线程/iterations:1",
                "run_type": "iteration", "submit_records_per_second": 10},
            {"name": "count_single_单线程/iterations:1",
                "run_type": "iteration", "submit_records_per_second": 20},
            {"name": "count_single_单线程/iterations:1_mean",
                "run_type": "aggregate", "submit_records_per_second": 15},
            {"name": "count_single_单线程/iterations:1",
                "run_type": "iteration", "error_occurred": True},
            {"name": "count_single_单线程/iterations:1",
                "run_type": "iteration", "submit_records_per_second": 30},
        ]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.json"
            path.write_text(json.dumps(
                data, ensure_ascii=False), encoding="utf-8")
            context, runs = plot_results.load_runs(path)

        self.assertEqual(context["dlog_records"], "10")
        self.assertEqual(plot_results.groups_for_cases(
            runs, "submit_records_per_second"), [("单线程计数", [(1, 10), (2, 20), (4, 30)])])
        self.assertEqual(plot_results.values(
            [{"drain_seconds": 0.002, "repetition_index": 2}], "drain_seconds", 1000), [(3, 2)])
        self.assertEqual(runs["latency"], [])

    def test_missing_rounds_leave_a_gap(self):
        fig, ax = plot_results.plt.subplots()
        try:
            plot_results.plot_panel(
                ax, [("场景", [(1, 10), (3, 30)])], "测试", "毫秒")
            line = ax.lines[0]
            self.assertEqual(list(line.get_xdata()), [1, 2, 3])
            self.assertTrue(math.isnan(line.get_ydata()[1]))
        finally:
            plot_results.plt.close(fig)

    def test_rejects_missing_results(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.json"
            path.write_text('{}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "benchmarks"):
                plot_results.load_runs(path)


if __name__ == "__main__":
    unittest.main()
