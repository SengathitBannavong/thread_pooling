#!/usr/bin/env python3
"""plot_latency.py — plot tail-latency results from latency_stats.py output.

Produces three figures in --outdir (default benchmark/plot/latency):
  - percentiles_<label>.png : p50/p90/p95/p99 vs concurrency
  - throughput_<label>.png  : achieved req/s vs concurrency
  - cdf_<label>.png         : latency CDF for a few concurrency levels

Usage:
    python demo/plot_latency.py [--dir demo/bench_results] [--label vps_pool]
                                [--outdir benchmark/plot/latency]
"""
import argparse
import glob
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="demo/bench_results")
    ap.add_argument("--label", default="vps_pool")
    ap.add_argument("--outdir", default="benchmark/plot/latency")
    args = ap.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    summ = os.path.join(args.dir, f"latency_summary_{args.label}.csv")
    df = pd.read_csv(summ).sort_values("concurrency")

    # ── 1. percentiles vs concurrency ──────────────────────────
    plt.figure(figsize=(10, 6))
    for col, lab, style in [("p50_ms", "p50", "o-"), ("p90_ms", "p90", "s-"),
                            ("p95_ms", "p95", "^-"), ("p99_ms", "p99", "d-")]:
        plt.plot(df["concurrency"], df[col], style, label=lab)
    plt.xscale("log", base=2)
    plt.xticks(df["concurrency"], [str(c) for c in df["concurrency"]])
    plt.xlabel("Concurrency (in-flight requests)")
    plt.ylabel("Latency (ms)")
    plt.title("Tail latency vs concurrency — demo render server")
    plt.grid(True, which="both")
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(args.outdir, f"percentiles_{args.label}.png"))
    plt.close()

    # ── 2. throughput vs concurrency ───────────────────────────
    plt.figure(figsize=(10, 6))
    plt.plot(df["concurrency"], df["throughput_rps"], "o-", color="seagreen")
    for x, y in zip(df["concurrency"], df["throughput_rps"]):
        plt.annotate(f"{y:.1f}", (x, y), textcoords="offset points",
                     xytext=(0, 8), ha="center", fontsize=9)
    plt.xscale("log", base=2)
    plt.xticks(df["concurrency"], [str(c) for c in df["concurrency"]])
    plt.xlabel("Concurrency (in-flight requests)")
    plt.ylabel("Throughput (req/s)")
    plt.title("Throughput vs concurrency — demo render server")
    plt.grid(True, which="both")
    plt.tight_layout()
    plt.savefig(os.path.join(args.outdir, f"throughput_{args.label}.png"))
    plt.close()

    # ── 3. latency CDF for a few concurrency levels ────────────
    raw_files = glob.glob(os.path.join(args.dir, f"raw_{args.label}_c*.txt"))
    by_c = {int(re.search(r"_c(\d+)\.txt$", p).group(1)): p for p in raw_files}
    present = sorted(by_c)
    # pick low / mid / high
    picks = sorted(set([present[0], present[len(present) // 2], present[-1]]))

    plt.figure(figsize=(10, 6))
    for C in picks:
        ms = []
        with open(by_c[C]) as f:
            for line in f:
                parts = line.split()
                if len(parts) == 2 and parts[0] == "200":
                    ms.append(float(parts[1]) * 1000.0)
        if not ms:
            continue
        ms.sort()
        ys = [(i + 1) / len(ms) for i in range(len(ms))]
        plt.plot(ms, ys, label=f"concurrency {C}")
    plt.xlabel("Latency (ms)")
    plt.ylabel("Cumulative fraction of requests")
    plt.title("Latency CDF — demo render server")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(args.outdir, f"cdf_{args.label}.png"))
    plt.close()

    print(f"figures written to {args.outdir}")


if __name__ == "__main__":
    main()
