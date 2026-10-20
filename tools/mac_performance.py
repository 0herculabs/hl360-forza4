"""Local process sampling and explicit-segment performance summaries."""
import csv
from datetime import datetime
from pathlib import Path
import re
import subprocess
import threading
import time


def cpu_seconds(value):
    days, _, value = value.rpartition("-") if "-" in value else ("0", "", value)
    fields = [float(part) for part in value.split(":")]
    total = 0.0
    for part in fields:
        total = total * 60 + part
    return int(days) * 86400 + total


class ProcessSampler:
    def __init__(self, pid, path):
        self.pid, self.path = pid, path
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def start(self):
        self.thread.start()

    def finish(self):
        self.stop.set()
        self.thread.join(timeout=3)

    def run(self):
        started = time.monotonic()
        with self.path.open("w") as output:
            writer = csv.writer(output)
            writer.writerow(["elapsed_s", "unix_s", "cpu_seconds", "rss_kib"])
            while not self.stop.is_set():
                try:
                    result = subprocess.run(["ps", "-p", str(self.pid), "-o", "time=", "-o", "rss="],
                                            capture_output=True, text=True, timeout=2)
                    fields = result.stdout.split()
                    if result.returncode or len(fields) != 2:
                        break
                    writer.writerow([f"{time.monotonic()-started:.6f}", f"{time.time():.6f}", cpu_seconds(fields[0]), int(fields[1])])
                    output.flush()
                except (OSError, ValueError, subprocess.TimeoutExpired):
                    break
                self.stop.wait(1.0)


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    index = (len(values)-1) * fraction
    lower = int(index)
    return values[lower] + (values[min(lower+1, len(values)-1)]-values[lower]) * (index-lower)


def summarize(run, start, end=None, label="unspecified"):
    run = Path(run)
    frames_path = run / "frames.csv"
    with frames_path.open() as source:
        frames = [row for row in csv.DictReader(source)
                  if float(row["elapsed_s"]) >= start and (end is None or float(row["elapsed_s"]) <= end)
                  and float(row["interval_ms"]) > 0]
    if not frames:
        raise ValueError("No frame samples in the selected segment")
    intervals = [float(row["interval_ms"]) for row in frames]
    present = [float(row["present_call_ms"]) for row in frames]
    result = {"label": label, "run": str(run.resolve()), "start_s": start, "end_s": end,
              "frames": len(frames), "guest_hook_fps": 1000*len(intervals)/sum(intervals),
              "frame_ms": {key: percentile(intervals, fraction) for key, fraction in
                           (("p50", .5), ("p95", .95), ("p99", .99), ("max", 1))},
              "stalls_over_50ms": sum(v > 50 for v in intervals),
              "stalls_over_100ms": sum(v > 100 for v in intervals),
              "present_call_mean_ms": sum(present)/len(present),
              "limitations": ["Guest present-hook intervals, not successful host presentation or simulation speed.",
                               "SDK CPU stage times are inclusive wall time; do not sum nested stages or threads.",
                               "No GPU execution timestamps; fence waits include queued GPU work and scheduling.",
                               "Select a stable scene explicitly; startup, loading and menus are not race benchmarks."]}
    samples_path = run / "process.csv"
    frame_epoch = float(frames[0]["unix_s"]) - float(frames[0]["elapsed_s"])
    if samples_path.exists():
        with samples_path.open() as source:
            samples = [row for row in csv.DictReader(source) if float(row["unix_s"]) >= frame_epoch + start
                       and (end is None or float(row["unix_s"]) <= frame_epoch + end)]
        if len(samples) > 1:
            duration = float(samples[-1]["elapsed_s"])-float(samples[0]["elapsed_s"])
            result["process_cpu_cores_mean"] = (float(samples[-1]["cpu_seconds"])-float(samples[0]["cpu_seconds"]))/duration
            result["rss_max_mib"] = max(int(row["rss_kib"]) for row in samples)/1024
    stages = {}
    pattern = re.compile(r"\[([^]]+)\].*\[t(\d+)\] perf_cpu: window_s=([\d.]+) stage=(\w+) calls=(\d+) total_ms=([\d.]+) max_ms=([\d.]+)")
    log = run / "fm4.log"
    if log.exists():
        with log.open(errors="replace") as source:
            for line in source:
                match = pattern.search(line)
                if not match:
                    continue
                stamp, thread, seconds, stage, calls, total, maximum = match.groups()
                timestamp = datetime.fromisoformat(stamp).timestamp()
                seconds = float(seconds)
                if timestamp - seconds < frame_epoch + start or (end is not None and timestamp > frame_epoch + end):
                    continue
                counter = stages.setdefault(thread, {}).setdefault(stage,
                    {"calls": 0, "inclusive_ms": 0.0, "max_ms": 0.0, "logged_window_s": 0.0})
                counter["calls"] += int(calls)
                counter["inclusive_ms"] += float(total)
                counter["max_ms"] = max(counter["max_ms"], float(maximum))
                counter["logged_window_s"] += seconds
    result["cpu_stages_by_thread"] = stages
    result["limitations"].append("Stage summaries exclude windows crossing segment boundaries; inactive final windows may not flush.")
    return result
