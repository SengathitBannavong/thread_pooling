#!/usr/bin/env python3
"""latency_stats.py — compute latency percentiles from bench_latency.sh raw data.

Reads raw_<label>_c<C>.txt files (one "<http_code> <time_total_s>" per line)
plus the meta_<label>.txt produced by demo/bench_latency.sh, and writes a tidy
summary CSV with avg / min / max / stddev / p50 / p90 / p95 / p99 / p999 and the
achieved throughput (req/s) for every concurrency level.

Latency statistics are computed over the SUCCESSFUL (HTTP 200) requests only;
failures are reported separately. Percentiles use the nearest-rank method.

Usage:
    python demo/latency_stats.py [--dir demo/bench_results] [--label vps_pool]
"""
import argparse
import glob
import math
import os
import re
import sys


def nearest_rank(sorted_vals, p):
    """Nearest-rank percentile (p in 0..100) over a sorted list."""
    n = len(sorted_vals)
    if n == 0:
        return float("nan")
    rank = math.ceil(p / 100.0 * n)
    rank = max(1, min(n, rank))
    return sorted_vals[rank - 1]


def parse_meta(meta_path):
    """Return {concurrency: wall_seconds} and the run url from a meta file."""
    walls, url = {}, ""
    if not os.path.exists(meta_path):
        return walls, url
    with open(meta_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("url="):
                url = line[4:]
            m = re.match(r"config c=(\d+) wall=([\d.]+) ok=(\d+)", line)
            if m:
                walls[int(m.group(1))] = float(m.group(2))
    return walls, url


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="demo/bench_results")
    ap.add_argument("--label", default="vps_pool")
    ap.add_argument("--out", default=None,
                    help="summary CSV path (default <dir>/latency_summary_<label>.csv)")
    args = ap.parse_args()

    walls, url = parse_meta(os.path.join(args.dir, f"meta_{args.label}.txt"))
    pattern = os.path.join(args.dir, f"raw_{args.label}_c*.txt")
    files = sorted(glob.glob(pattern),
                   key=lambda p: int(re.search(r"_c(\d+)\.txt$", p).group(1)))
    if not files:
        sys.exit(f"no raw files matching {pattern}")

    out = args.out or os.path.join(args.dir, f"latency_summary_{args.label}.csv")
    header = ("concurrency,total,ok,fail,throughput_rps,"
              "avg_ms,min_ms,max_ms,stddev_ms,p50_ms,p90_ms,p95_ms,p99_ms,p999_ms")
    rows = [header]

    print(f"URL: {url}")
    print(f"{'conc':>5} {'ok':>5} {'fail':>5} {'rps':>7} "
          f"{'avg':>8} {'p50':>8} {'p90':>8} {'p95':>8} {'p99':>8} {'p999':>8}")

    for fp in files:
        C = int(re.search(r"_c(\d+)\.txt$", fp).group(1))
        ok_ms, total, fail = [], 0, 0
        with open(fp) as f:
            for line in f:
                parts = line.split()
                if len(parts) != 2:
                    continue
                total += 1
                code, t = parts[0], parts[1]
                try:
                    ms = float(t) * 1000.0
                except ValueError:
                    fail += 1
                    continue
                if code == "200":
                    ok_ms.append(ms)
                else:
                    fail += 1

        ok_ms.sort()
        n = len(ok_ms)
        wall = walls.get(C, 0.0)
        rps = (n / wall) if wall > 0 else 0.0
        if n:
            avg = sum(ok_ms) / n
            var = sum((x - avg) ** 2 for x in ok_ms) / (n - 1) if n > 1 else 0.0
            sd = math.sqrt(var)
            mn, mx = ok_ms[0], ok_ms[-1]
            p50 = nearest_rank(ok_ms, 50)
            p90 = nearest_rank(ok_ms, 90)
            p95 = nearest_rank(ok_ms, 95)
            p99 = nearest_rank(ok_ms, 99)
            p999 = nearest_rank(ok_ms, 99.9)
        else:
            avg = sd = mn = mx = p50 = p90 = p95 = p99 = p999 = float("nan")

        rows.append(f"{C},{total},{n},{fail},{rps:.2f},{avg:.2f},{mn:.2f},"
                    f"{mx:.2f},{sd:.2f},{p50:.2f},{p90:.2f},{p95:.2f},"
                    f"{p99:.2f},{p999:.2f}")
        print(f"{C:>5} {n:>5} {fail:>5} {rps:>7.1f} "
              f"{avg:>8.1f} {p50:>8.1f} {p90:>8.1f} {p95:>8.1f} {p99:>8.1f} {p999:>8.1f}")

    with open(out, "w") as f:
        f.write("\n".join(rows) + "\n")
    print(f"\nsummary written to {out}")


if __name__ == "__main__":
    main()
