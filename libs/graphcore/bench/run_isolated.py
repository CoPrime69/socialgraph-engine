"""Runs every graphcore benchmark in its own process and re-runs it when other processes on the
(shared) machine used noticeable CPU while it ran.

interference = (busy CPU time of the whole machine from /proc/stat) - (CPU time of the benchmark
process, from getrusage(RUSAGE_CHILDREN)). A run is accepted when interference <= 0.15 * wall + 0.2 s.

Usage (Linux only, reads /proc/stat):
  python3 -I run_isolated.py BENCH_BINARY OUT_DIR [FILTER=.] [REPETITIONS=3] [MAX_ATTEMPTS=8]
Writes OUT_DIR/merged.json (Google Benchmark JSON of the accepted runs) and OUT_DIR/runs.log.
"""
import json
import os
import re
import resource
import subprocess
import sys
import time

HZ = os.sysconf("SC_CLK_TCK")


def busy_seconds():
    with open("/proc/stat") as f:
        fields = [int(x) for x in f.readline().split()[1:]]
    user, nice, system, idle, iowait, irq, softirq = fields[:7]
    return (user + nice + system + irq + softirq) / HZ


def child_cpu():
    r = resource.getrusage(resource.RUSAGE_CHILDREN)
    return r.ru_utime + r.ru_stime


def main(binary, out_dir, name_filter, repetitions, max_attempts):
    os.makedirs(out_dir, exist_ok=True)
    names = subprocess.run([binary, "--benchmark_list_tests", f"--benchmark_filter={name_filter}"],
                           check=True, capture_output=True, text=True).stdout.split()
    log = open(os.path.join(out_dir, "runs.log"), "a")
    merged = None
    for name in names:
        best = None
        for attempt in range(1, max_attempts + 1):
            tmp = os.path.join(out_dir, "current.json")
            b0, c0, t0 = busy_seconds(), child_cpu(), time.monotonic()
            proc = subprocess.run([binary, f"--benchmark_filter=^{re.escape(name)}$",
                                   f"--benchmark_repetitions={repetitions}",
                                   "--benchmark_display_aggregates_only=true",
                                   f"--benchmark_out={tmp}", "--benchmark_out_format=json"],
                                  capture_output=True, text=True)
            t1, c1, b1 = time.monotonic(), child_cpu(), busy_seconds()
            wall, own = t1 - t0, c1 - c0
            interference = (b1 - b0) - own
            ok = proc.returncode == 0 and interference <= 0.15 * wall + 0.2
            line = (f"{time.strftime('%H:%M:%S')} {name} attempt {attempt} rc {proc.returncode} "
                    f"wall {wall:.1f}s own_cpu {own:.1f}s interference {interference:.2f}s "
                    f"{'OK' if ok else 'REJECTED'}")
            print(line, flush=True)
            log.write(line + "\n")
            log.flush()
            if proc.returncode != 0:
                print(proc.stdout[-2000:], proc.stderr[-2000:], flush=True)
            data = json.load(open(tmp)) if proc.returncode == 0 else None
            if data is not None and (best is None or interference < best[0]):
                best = (interference, data, attempt)
            if ok:
                break
            time.sleep(20)
        if best is None:
            continue
        interference, data, attempt = best
        for b in data["benchmarks"]:
            b["interference_seconds"] = interference
            b["attempt"] = attempt
        if merged is None:
            merged = data
        else:
            merged["benchmarks"].extend(data["benchmarks"])
        json.dump(merged, open(os.path.join(out_dir, "merged.json"), "w"), indent=1)
    print("done", flush=True)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ".",
         int(sys.argv[4]) if len(sys.argv) > 4 else 3, int(sys.argv[5]) if len(sys.argv) > 5 else 8)
