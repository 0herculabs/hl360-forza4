import csv
from datetime import datetime
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from mac_performance import cpu_seconds, percentile, summarize

class PerformanceTests(unittest.TestCase):
    def test_cpu_time_formats(self):
        self.assertEqual(cpu_seconds("1:02.50"), 62.5)
        self.assertEqual(cpu_seconds("1-02:03:04.25"), 93784.25)

    def test_percentiles(self):
        self.assertEqual(percentile([30, 10, 20], .95), 29)
        self.assertEqual(percentile([7], .99), 7)
        self.assertIsNone(percentile([], .5))

    def test_segment_alignment_and_nested_stage_accounting(self):
        with tempfile.TemporaryDirectory() as directory:
            run = Path(directory)
            epoch = datetime(2026, 10, 16, 12).timestamp()
            with (run / "frames.csv").open("w") as output:
                writer = csv.writer(output)
                writer.writerow(["frame", "elapsed_s", "unix_s", "interval_ms", "present_call_ms"])
                writer.writerows([[1,0,epoch,0,2],[2,1,epoch+1,1000,3],
                                  [3,2,epoch+2,20,4],[4,3,epoch+3,30,6]])
            with (run / "process.csv").open("w") as output:
                writer = csv.writer(output)
                writer.writerow(["elapsed_s", "unix_s", "cpu_seconds", "rss_kib"])
                writer.writerows([[12,epoch+2,4,1024],[13,epoch+3,6,2048]])
            stamp = datetime.fromtimestamp(epoch+3).isoformat(sep=" ", timespec="milliseconds")
            (run / "fm4.log").write_text(
                f"[{stamp}] [info] [core] [t7] perf_cpu: window_s=1.000000 stage=draw calls=2 total_ms=20.000 max_ms=12.000\n"
                f"[{stamp}] [info] [core] [t7] perf_cpu: window_s=1.000000 stage=fence_wait calls=1 total_ms=10.000 max_ms=10.000\n")
            result = summarize(run, 2, 3, "fixture")
            self.assertEqual(result["frames"], 2)
            self.assertEqual(result["guest_hook_fps"], 40)
            self.assertEqual(result["present_call_mean_ms"], 5)
            self.assertEqual(result["process_cpu_cores_mean"], 2)
            self.assertEqual(result["rss_max_mib"], 2)
            self.assertEqual(result["cpu_stages_by_thread"]["7"]["draw"]["inclusive_ms"], 20)
            self.assertEqual(result["cpu_stages_by_thread"]["7"]["fence_wait"]["inclusive_ms"], 10)
            with self.assertRaises(ValueError):
                summarize(run, 100, 101, "empty")

if __name__ == "__main__":
    unittest.main()
