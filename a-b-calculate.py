#!/usr/bin/env python3
"""
hetero_stats.py — calculate mean ± SD, CV, ratio, Welch t-test
from raw benchmark log/CSV to be the source of truth for the table in the report

  python3 hetero_stats.py interleaved_log.txt          # log  Mine:/Base:
  python3 hetero_stats.py mine.csv base.csv            # CSV
"""

import math
import sys
from collections import defaultdict

HEADER = "method,n_tasks,run,total_time_us,throughput,avg_latency_us,mem_run_cost_kb"


def parse_file(path, default_side=None):
    """Return dict: data[side][workers] = list of throughput"""
    data = defaultdict(lambda: defaultdict(list))
    side = default_side
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            low = line.lower()
            if low.startswith("mine"):
                side = "mine"
                continue
            if low.startswith("base"):
                side = "base"
                continue
            if not line or line.startswith("-") or low.startswith("run "):
                continue
            if low.startswith("method,"):
                continue
            parts = line.split(",")
            if len(parts) != 7:
                continue
            if side is None:
                sys.exit(
                f"Found data before knowing which side: {line}\n"
                f"If using pure CSV, provide two files (mine then base)"
            )
            workers = int(parts[0].split("_")[0])  # "8_workers" -> 8
            throughput = float(parts[4])
            data[side][workers].append(throughput)
    return data


def merge(a, b):
    for side in b:
        for w in b[side]:
            a[side][w].extend(b[side][w])
    return a


def mean_sd(xs):
    n = len(xs)
    m = sum(xs) / n
    if n < 2:
        return m, 0.0
    var = sum((x - m) ** 2 for x in xs) / (n - 1)  # Bessel
    return m, math.sqrt(var)


def welch_t(x, y):
    """Return (t, df) using Welch–Satterthwaite"""
    mx, sx = mean_sd(x)
    my, sy = mean_sd(y)
    nx, ny = len(x), len(y)
    vx, vy = sx**2 / nx, sy**2 / ny
    t = (mx - my) / math.sqrt(vx + vy)
    df = (vx + vy) ** 2 / (vx**2 / (nx - 1) + vy**2 / (ny - 1))
    return t, df


def main():
    args = sys.argv[1:]
    if len(args) == 1:
        data = parse_file(args[0])
    elif len(args) == 2:
        data = merge(parse_file(args[0], "mine"), parse_file(args[1], "base"))
    else:
        sys.exit(__doc__)

    if not data["mine"] or not data["base"]:
        sys.exit("Missing data for both sides — check file format")

    workers_all = sorted(set(data["mine"]) & set(data["base"]))

    print(
        f"{'Workers':>7} | {'Mine (t/s)':>22} | {'Baseline (t/s)':>22} | "
        f"{'Ratio':>6} | {'CV m/b':>13} | {'Welch t (df)':>14} | sig?"
    )
    print("-" * 105)

    latex_rows = []
    for w in workers_all:
        xm, xb = data["mine"][w], data["base"][w]
        mm, sm = mean_sd(xm)
        mb, sb = mean_sd(xb)
        ratio = mm / mb
        cvm = 100 * sm / mm
        cvb = 100 * sb / mb
        t, df = welch_t(xm, xb)
        # Rough threshold: |t| > ~2.2 at df~9 ≈ p<0.05
        sig = "yes" if abs(t) > 2.2 else "NO (tie)"
        print(
            f"{w:>7} | {mm:>12,.0f} ± {sm:>6,.0f} | "
            f"{mb:>12,.0f} ± {sb:>6,.0f} | {ratio:>5.2f}x | "
            f"{cvm:>5.2f}%/{cvb:>5.2f}% | {t:>7.2f} ({df:>4.1f}) | {sig}"
        )
        latex_rows.append(
            f"{w} & {mm:,.0f} $\\pm$ {sm:,.0f} & "
            f"{mb:,.0f} $\\pm$ {sb:,.0f} & {ratio:.2f}x \\\\".replace(",", "\\,")
        )

    print("\n--- LaTeX rows (paste into table) ---")
    for r in latex_rows:
        print(r)

    print(
        f"\nNumber of runs: mine={ {w: len(data['mine'][w]) for w in workers_all} }, "
        f"base={ {w: len(data['base'][w]) for w in workers_all} }"
    )


if __name__ == "__main__":
    main()
