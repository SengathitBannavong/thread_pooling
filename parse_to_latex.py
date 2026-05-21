#!/usr/bin/env python3
"""
Benchmark Analysis Script - LaTeX Table Generator
===============================================
Reads benchmark CSV files, computes statistics (mean, sample stddev, CV),
and generates LaTeX tables for the thesis report.

To add new benchmarks:
1. Ensure the CSV has a 'method' column, 'run' column, and the target metric column.
2. Add the filename to the parsing section in the `main` function.
3. Extract the target values using `extract_values`.
4. Add the formatted output to the LaTeX generation and console reporting sections.

Usage:
  python analyze.py
  python analyze.py --input-dir custom/path --output tables.tex --verbose
"""

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path


def compute_stats(values):
    n = len(values)
    if n == 0:
        return {"n": 0, "mean": 0.0, "stdev": 0.0, "cv": 0.0, "min": 0.0, "max": 0.0}

    # Sanity check: Values should be positive
    for v in values:
        if v <= 0:
            print(f"[WARNING] Non-positive value detected: {v}", file=sys.stderr)

    mean = statistics.mean(values)
    if n >= 2:
        stdev = statistics.stdev(values)  # Sample stdev (n-1)
        cv = (stdev / mean * 100) if mean != 0 else 0
    else:
        stdev = 0.0
        cv = 0.0
        print(
            "[WARNING] Only 1 trial found; sample standard deviation is 0.",
            file=sys.stderr,
        )

    if cv > 30.0:
        print(
            f"[WARNING] High coefficient of variation detected: {cv:.2f}%",
            file=sys.stderr,
        )

    return {
        "n": n,
        "mean": mean,
        "stdev": stdev,
        "cv": cv,
        "min": min(values),
        "max": max(values),
    }


def read_csv(filepath):
    if not filepath.exists():
        print(f"[WARNING] File not found, skipping: {filepath}", file=sys.stderr)
        return []

    data = []
    try:
        with open(filepath, "r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                data.append(row)
    except Exception as e:
        print(f"[ERROR] Failed to read {filepath}: {e}", file=sys.stderr)
    return data


def extract_values(data, target_method, metric_col, verbose=False, filepath=""):
    # Group by run to handle potential duplicates (though we rely on exact method match)
    runs = defaultdict(list)
    for row in data:
        if row.get("method") == target_method:
            try:
                run_id = int(row.get("run", 1))
                val = float(row.get(metric_col, 0.0))
                runs[run_id].append(val)
            except ValueError:
                continue

    values = []
    # If duplicates exist per run (like sequential), we take the first or average?
    # Spec says deduplicate by treating sequential as single, but for pool we shouldn't have dups.
    # We will just take the first entry of each run ID for safety.
    for run_id in sorted(runs.keys()):
        values.append(runs[run_id][0])

    if verbose and values:
        print(f"  [Verbose] {filepath.name} ({target_method}): {values}")

    return values


def fmt_num(val):
    return f"{val:,.1f}".replace(",", "\\,")


def fmt_std(val):
    # Round stddev to 1 decimal place generally
    return f"{val:,.1f}".replace(",", "\\,")


def fmt_cv(val):
    return f"{val:.2f}\\%"


def align_trials(mine_vals, base_vals, name=""):
    n_mine = len(mine_vals)
    n_base = len(base_vals)
    if n_mine != n_base and n_mine > 0 and n_base > 0:
        n_min = min(n_mine, n_base)
        print(
            f"[WARNING] Trial counts differ for {name} (Mine: {n_mine}, Base: {n_base}). Truncating to {n_min}.",
            file=sys.stderr,
        )
        return mine_vals[:n_min], base_vals[:n_min]
    return mine_vals, base_vals


def analyze_comparison(mine_stats, base_stats, name, verdicts):
    if mine_stats["n"] == 0 or base_stats["n"] == 0:
        return None

    ratio = mine_stats["mean"] / base_stats["mean"] if base_stats["mean"] > 0 else 0
    stability_ratio = base_stats["cv"] / mine_stats["cv"] if mine_stats["cv"] > 0 else 0

    if ratio > 1.02:
        verdicts["wins"].append(f"{name} ({ratio:.2f}x)")
    elif ratio < 0.98:
        verdicts["losses"].append(f"{name} ({ratio:.2f}x)")
    else:
        verdicts["ties"].append(name)

    if stability_ratio > 2.0:
        verdicts["stability"].append(f"{name} ({stability_ratio:.1f}x)")

    return ratio, stability_ratio


def main():
    parser = argparse.ArgumentParser(
        description="Generate benchmark statistics and LaTeX tables."
    )
    parser.add_argument(
        "--input-dir",
        type=str,
        default="benchmark/res",
        help="Directory containing CSV files",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="benchmark/res/tables.tex",
        help="Output path for LaTeX tables",
    )
    parser.add_argument(
        "--verbose", action="store_true", help="Print individual trial values"
    )
    args = parser.parse_args()

    input_dir = Path(args.input_dir)
    verdicts = {"wins": [], "losses": [], "ties": [], "stability": []}

    print("=" * 60)
    print("Thread Pool Benchmark Analysis")
    print(f"Run Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("Methodology: Mean ± sample stddev (n-1), CV%")
    print("=" * 60 + "\n")

    # 1. CPU and IO Bound Data
    real_workload_rows = []

    # CPU
    f_cpu_mine = input_dir / "result_cpu_bound_n_100000.txt"
    f_cpu_base = input_dir / "result_base_cpu_bound_n_100000.txt"
    mine_cpu_vals = extract_values(
        read_csv(f_cpu_mine), "thread_pool", "throughput", args.verbose, f_cpu_mine
    )
    base_cpu_vals = extract_values(
        read_csv(f_cpu_base), "baseline", "throughput", args.verbose, f_cpu_base
    )
    mine_cpu_vals, base_cpu_vals = align_trials(
        mine_cpu_vals, base_cpu_vals, "CPU-bound"
    )

    stat_mine_cpu = compute_stats(mine_cpu_vals)
    stat_base_cpu = compute_stats(base_cpu_vals)
    cpu_ratios = analyze_comparison(stat_mine_cpu, stat_base_cpu, "CPU bound", verdicts)

    if cpu_ratios:
        real_workload_rows.append(
            f"CPU-bound (fib30, 100K) & {fmt_num(stat_mine_cpu['mean'])} $\\pm$ {fmt_std(stat_mine_cpu['stdev'])} & "
            f"{fmt_num(stat_base_cpu['mean'])} $\\pm$ {fmt_std(stat_base_cpu['stdev'])} & {cpu_ratios[0]:.3f}x & "
            f"{fmt_cv(stat_mine_cpu['cv'])} / {fmt_cv(stat_base_cpu['cv'])} \\\\"
        )
        print("[CPU-bound 100K tasks]")
        print(
            f"  Mine: {stat_mine_cpu['mean']:.1f} ± {stat_mine_cpu['stdev']:.1f} t/s (CV={stat_mine_cpu['cv']:.2f}%)"
        )
        print(
            f"  Base: {stat_base_cpu['mean']:.1f} ± {stat_base_cpu['stdev']:.1f} t/s (CV={stat_base_cpu['cv']:.2f}%)"
        )
        print(f"  Ratio: {cpu_ratios[0]:.4f}x\n")

    # IO
    f_io_mine = input_dir / "result_io_bound_n_100000.txt"
    f_io_base = input_dir / "result_base_io_bound_n_100000.txt"
    mine_io_vals = extract_values(
        read_csv(f_io_mine), "thread_pool", "throughput", args.verbose, f_io_mine
    )
    base_io_vals = extract_values(
        read_csv(f_io_base), "baseline", "throughput", args.verbose, f_io_base
    )
    mine_io_vals, base_io_vals = align_trials(mine_io_vals, base_io_vals, "IO-bound")

    stat_mine_io = compute_stats(mine_io_vals)
    stat_base_io = compute_stats(base_io_vals)
    io_ratios = analyze_comparison(stat_mine_io, stat_base_io, "IO bound", verdicts)

    if io_ratios:
        real_workload_rows.append(
            f"IO-bound (64KB read, 100K) & {fmt_num(stat_mine_io['mean'])} $\\pm$ {fmt_std(stat_mine_io['stdev'])} & "
            f"{fmt_num(stat_base_io['mean'])} $\\pm$ {fmt_std(stat_base_io['stdev'])} & {io_ratios[0]:.3f}x & "
            f"{fmt_cv(stat_mine_io['cv'])} / {fmt_cv(stat_base_io['cv'])} \\\\"
        )
        print("[IO-bound 100K tasks]")
        print(
            f"  Mine: {stat_mine_io['mean']:.1f} ± {stat_mine_io['stdev']:.1f} t/s (CV={stat_mine_io['cv']:.2f}%)"
        )
        print(
            f"  Base: {stat_base_io['mean']:.1f} ± {stat_base_io['stdev']:.1f} t/s (CV={stat_base_io['cv']:.2f}%)"
        )
        print(f"  Ratio: {io_ratios[0]:.4f}x\n")

    # 2. Worker Scaling
    f_scale_mine = input_dir / "result_scaling.txt"
    f_scale_base = input_dir / "result_base_scaling.txt"
    scale_mine_data = read_csv(f_scale_mine)
    scale_base_data = read_csv(f_scale_base)

    workers = [1, 2, 4, 8, 16, 32, 64]
    scaling_rows = []
    scaling_variance_rows = []

    if scale_mine_data and scale_base_data:
        print("[Worker scaling]")
        for w in workers:
            method_name = f"{w}_workers"
            m_vals = extract_values(
                scale_mine_data, method_name, "throughput", args.verbose, f_scale_mine
            )
            b_vals = extract_values(
                scale_base_data, method_name, "throughput", args.verbose, f_scale_base
            )
            m_vals, b_vals = align_trials(m_vals, b_vals, f"Scaling {w}w")

            st_m = compute_stats(m_vals)
            st_b = compute_stats(b_vals)
            ratios = analyze_comparison(st_m, st_b, f"scaling {w}w", verdicts)

            if ratios:
                scaling_rows.append(
                    f"{w} & {fmt_num(st_m['mean'])} $\\pm$ {fmt_std(st_m['stdev'])} & "
                    f"{fmt_num(st_b['mean'])} $\\pm$ {fmt_std(st_b['stdev'])} & {ratios[0]:.3f}x \\\\"
                )
                scaling_variance_rows.append((f"Scaling {w}w", st_m, st_b, ratios[1]))
                print(
                    f"  {w:2d}w -> Mine: {st_m['mean']:8.1f} | Base: {st_b['mean']:8.1f} | Ratio: {ratios[0]:.3f}x"
                )
        print()

    # 3. Queue Ops
    f_queue_mine = input_dir / "result_queue_ops.txt"
    f_queue_base = input_dir / "result_base_queue_ops.txt"
    queue_mine_data = read_csv(f_queue_mine)
    queue_base_data = read_csv(f_queue_base)

    queue_workers = [1, 2, 4, 8, 16]
    queue_rows = []
    queue_variance_rows = []

    if queue_mine_data and queue_base_data:
        print("[Queue Operations]")
        for w in queue_workers:
            method_name = f"{w}_workers"
            m_vals = extract_values(
                queue_mine_data,
                method_name,
                "conc_queue_ops_per_sec",
                args.verbose,
                f_queue_mine,
            )
            b_vals = extract_values(
                queue_base_data,
                method_name,
                "conc_queue_ops_per_sec",
                args.verbose,
                f_queue_base,
            )
            m_vals, b_vals = align_trials(m_vals, b_vals, f"Queue {w}w")

            st_m = compute_stats(m_vals)
            st_b = compute_stats(b_vals)
            ratios = analyze_comparison(st_m, st_b, f"queue ops {w}w", verdicts)

            if ratios:
                queue_rows.append(
                    f"{w} & {fmt_num(st_m['mean'])} $\\pm$ {fmt_std(st_m['stdev'])} & "
                    f"{fmt_num(st_b['mean'])} $\\pm$ {fmt_std(st_b['stdev'])} & {ratios[0]:.3f}x \\\\"
                )
                queue_variance_rows.append((f"Queue {w}w", st_m, st_b, ratios[1]))
                print(
                    f"  {w:2d}w -> Mine: {st_m['mean']:10.1f} | Base: {st_b['mean']:10.1f} | Ratio: {ratios[0]:.3f}x"
                )
        print()

    # Build Variance Table rows
    var_rows = []
    if cpu_ratios:
        var_rows.append(
            f"CPU-bound & {fmt_cv(stat_mine_cpu['cv'])} & {fmt_cv(stat_base_cpu['cv'])} & {cpu_ratios[1]:.2f}x \\\\"
        )
    if io_ratios:
        var_rows.append(
            f"IO-bound & {fmt_cv(stat_mine_io['cv'])} & {fmt_cv(stat_base_io['cv'])} & {io_ratios[1]:.2f}x \\\\"
        )
    for name, m, b, r in scaling_variance_rows:
        var_rows.append(
            f"{name} & {fmt_cv(m['cv'])} & {fmt_cv(b['cv'])} & {r:.2f}x \\\\"
        )
    for name, m, b, r in queue_variance_rows:
        var_rows.append(
            f"{name} & {fmt_cv(m['cv'])} & {fmt_cv(b['cv'])} & {r:.2f}x \\\\"
        )

    # Write LaTeX Tables
    latex_out = ""

    # Table 1
    latex_out += "\\begin{table}[htbp]\n\\centering\n"
    latex_out += "\\caption{Real-workload throughput comparison (mean $\\pm$ sample stddev, $n=3$ trials)}\n"
    latex_out += "\\label{tab:real_workload}\n\\begin{tabular}{lrrrr}\n\\toprule\n"
    latex_out += "Benchmark & Mine (t/s) & Baseline (t/s) & Ratio & CV (mine / base) \\\\\n\\midrule\n"
    latex_out += "\n".join(real_workload_rows) + "\n"
    latex_out += "\\bottomrule\n\\end{tabular}\n\\end{table}\n\n"

    # Table 2
    latex_out += "\\begin{table}[htbp]\n\\centering\n"
    latex_out += (
        "\\caption{Worker scaling throughput (fib25 task, 10K tasks, $n=3$ trials)}\n"
    )
    latex_out += "\\label{tab:scaling}\n\\begin{tabular}{rrrr}\n\\toprule\n"
    latex_out += "Workers & Mine (t/s) & Baseline (t/s) & Ratio \\\\\n\\midrule\n"
    latex_out += "\n".join(scaling_rows) + "\n"
    latex_out += "\\bottomrule\n\\end{tabular}\n\\end{table}\n\n"

    # Table 3
    latex_out += "\\begin{table}[htbp]\n\\centering\n"
    latex_out += "\\caption{Concurrent queue operations throughput (10K tasks per trial, $n=3$ trials)}\n"
    latex_out += "\\label{tab:queue_ops}\n\\begin{tabular}{rrrr}\n\\toprule\n"
    latex_out += "Workers & Mine (ops/s) & Baseline (ops/s) & Ratio \\\\\n\\midrule\n"
    latex_out += "\n".join(queue_rows) + "\n"
    latex_out += "\\bottomrule\n\\end{tabular}\n\\end{table}\n\n"

    # Table 4
    latex_out += "\\begin{table}[htbp]\n\\centering\n"
    latex_out += (
        "\\caption{Coefficient of variation comparison (lower is more predictable)}\n"
    )
    latex_out += "\\label{tab:variance}\n\\begin{tabular}{lrrr}\n\\toprule\n"
    latex_out += "Benchmark & Mine CV (\\%) & Base CV (\\%) & Stability advantage \\\\\n\\midrule\n"
    latex_out += "\n".join(var_rows) + "\n"
    latex_out += "\\bottomrule\n\\end{tabular}\n\\end{table}\n"

    # Save to file
    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(latex_out)

    # Print Verdicts
    print("[Verdict]")
    print(
        f"  Mine wins (>1.02x): {', '.join(verdicts['wins']) if verdicts['wins'] else 'None'}"
    )
    print(
        f"  Tied (0.98-1.02x): {', '.join(verdicts['ties']) if verdicts['ties'] else 'None'}"
    )
    print(
        f"  Base wins (<0.98x): {', '.join(verdicts['losses']) if verdicts['losses'] else 'None'}"
    )
    print(
        f"  Stability advantage (Ratio > 2): {', '.join(verdicts['stability']) if verdicts['stability'] else 'None'}"
    )
    print(f"\nLaTeX tables successfully written to: {out_path}")


if __name__ == "__main__":
    main()
