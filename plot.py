import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import sys
import os

def plot_benchmark(csv_path, output_dir):
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Error reading CSV: {e}")
        return

    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    base_name = os.path.basename(csv_path).replace('.txt', '').replace('.csv', '')

    if "queue_ops" in base_name:
        _plot_queue_ops(df, base_name, output_dir)
        return

    if "aging" in base_name:
        _plot_aging(df, base_name, output_dir)
        return

    is_scaling   = "scaling"   in base_name
    is_stability = "stability" in base_name

    x_col = 'run'
    if is_scaling:
        df['workers'] = df['method'].str.extract(r'(\d+)').astype(int)
        df = df.sort_values('workers')
        x_col = 'workers'

    # ── 1. Throughput ──────────────────────────────────────────
    if is_scaling:
        def _fmt_tps(v):
            if v >= 1e6: return f"{v/1e6:.2f}M"
            if v >= 1e3: return f"{v/1e3:.1f}K"
            return f"{v:.0f}"

        # Average the n trials per worker count so each worker count is a
        # single bar (the raw CSV has one row per trial → 3x bars otherwise,
        # which collides the x-axis labels).
        agg  = df.groupby('workers')['throughput'].mean().sort_index()
        ws   = agg.index.astype(str).tolist()
        tps  = agg.tolist()
        x    = range(len(ws))

        fig, ax = plt.subplots(figsize=(11, 6))
        bars = ax.bar(x, tps, color='steelblue', width=0.55)
        ax.bar_label(bars,
                     labels=[_fmt_tps(v) for v in tps],
                     fontsize=9, padding=4)
        ax.set_xticks(list(x))
        ax.set_xticklabels([f"{w} workers" for w in ws])
        ax.set_xlabel('Number of Workers')
        ax.set_ylabel('Throughput (Tasks/sec)')
        ax.set_title(f'Throughput vs Worker Count - {base_name}')
        ax.grid(True, axis='y')
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, f'throughput_{base_name}.png'))
        plt.close()
    else:
        plt.figure(figsize=(10, 6))
        for method in df['method'].unique():
            md = df[df['method'] == method]
            plt.plot(md[x_col], md['throughput'], marker='o', label=method)
        plt.xlabel('Run / Cycle Number')
        plt.legend()
        plt.title(f'Throughput - {base_name}')
        plt.ylabel('Throughput (Tasks/sec)')
        plt.grid(True)
        plt.savefig(os.path.join(output_dir, f'throughput_{base_name}.png'))
        plt.close()

    # ── 2. Latency ─────────────────────────────────────────────
    plt.figure(figsize=(10, 6))
    if is_scaling:
        lat = df.groupby('workers')['avg_latency_us'].mean().sort_index()
        plt.plot(lat.index, lat.values, marker='o', color='orange')
        plt.xscale('log', base=2)
        plt.xticks(lat.index, [str(w) for w in lat.index])
        plt.xlabel('Number of Workers')
    else:
        for method in df['method'].unique():
            md = df[df['method'] == method]
            plt.plot(md[x_col], md['avg_latency_us'], marker='o', label=method)
        plt.xlabel('Run / Cycle Number')
        plt.legend()
    plt.title(f'Average Latency - {base_name}')
    plt.ylabel('Latency (us)')
    plt.grid(True)
    plt.savefig(os.path.join(output_dir, f'latency_{base_name}.png'))
    plt.close()

    # ── 3. Average throughput bar (non-scaling, non-stability) ─
    if not is_stability and not is_scaling:
        plt.figure(figsize=(10, 6))
        df.groupby('method')['throughput'].mean().plot(kind='bar')
        plt.title(f'Average Throughput - {base_name}')
        plt.ylabel('Throughput (Tasks/sec)')
        plt.xticks(rotation=45)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, f'avg_throughput_{base_name}.png'))
        plt.close()

    # ── 4. Memory cost (single column) ─────────────────────────
    # scaling uses mem_pool_cost_kb (pool creation cost = mem_init - mem_base)
    # all other benchmarks use mem_run_cost_kb (extra RSS while tasks ran)
    mem_col = 'mem_pool_cost_kb' if 'mem_pool_cost_kb' in df.columns else \
              'mem_run_cost_kb'  if 'mem_run_cost_kb'  in df.columns else None
    if mem_col:
        _plot_memory(df, base_name, output_dir, is_scaling, x_col, mem_col)

    # ── 5. Speedup S_p = t1 / tp (scaling only) ────────────────
    if is_scaling and 'total_time_us' in df.columns:
        _plot_speedup(df, base_name, output_dir)

    print(f"Plots generated for {base_name} in {output_dir}")


def _plot_speedup(df, base_name, output_dir):
    """Speedup vs worker count: S_p = t1 / tp, averaged across runs.

    t1 is the mean single-worker wall time; tp the mean wall time at p workers.
    Overlays the ideal-linear reference (S_p = p) so the Amdahl plateau is
    visible as the gap between the measured curve and the diagonal.
    """
    avg = df.groupby('workers')['total_time_us'].mean().sort_index()
    if 1 not in avg.index:
        print(f"  [speedup] no 1-worker row in {base_name}; skipping")
        return
    t1 = avg.loc[1]
    ws = avg.index.tolist()
    sp = (t1 / avg).tolist()

    plt.figure(figsize=(10, 6))
    plt.plot(ws, sp, marker='o', color='seagreen', label='Measured $S_p = t_1/t_p$')
    plt.plot(ws, ws, linestyle='--', color='gray', label='Ideal linear ($S_p = p$)')
    for w, s in zip(ws, sp):
        plt.annotate(f"{s:.2f}", (w, s), textcoords="offset points",
                     xytext=(0, 8), ha='center', fontsize=9)
    plt.xscale('log', base=2)
    plt.xticks(ws, [str(w) for w in ws])
    plt.xlabel('Number of Workers')
    plt.ylabel('Speedup $S_p$')
    plt.title(f'Speedup vs Worker Count - {base_name}')
    plt.grid(True, which='both')
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'speedup_{base_name}.png'))
    plt.close()


def _plot_memory(df, base_name, output_dir, is_scaling, x_col, mem_col='mem_run_cost_kb'):
    """Single-column memory bar chart.

    mem_col is 'mem_pool_cost_kb' for the scaling benchmark (pool creation
    cost, grows with worker count) or 'mem_run_cost_kb' for all other
    benchmarks (extra RSS while tasks ran).
    """
    if is_scaling:
        ylabel = 'Total RSS overhead (KB)\n(pool stacks + task heap, vs clean baseline)'
        title  = f'Memory Overhead vs Worker Count - {base_name}'
    else:
        ylabel = f'{mem_col} (KB)'
        title  = f'Memory Cost While Running - {base_name}'

    plt.figure(figsize=(10, 5))

    if is_scaling:
        agg = df.groupby('workers')[mem_col].mean().sort_index()
        plt.bar(agg.index.astype(str), agg.values, color='steelblue')
        plt.xlabel('Number of Workers')
    else:
        avg = df.groupby('method')[mem_col].mean()
        avg.plot(kind='bar', color='steelblue')
        plt.xticks(rotation=30, ha='right')

    plt.axhline(0, color='red', linewidth=0.8, linestyle='--')
    plt.title(title)
    plt.ylabel(ylabel)
    plt.grid(True, axis='y')
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'memory_{base_name}.png'))
    plt.close()


def _plot_queue_ops(df, base_name, output_dir):
    """Queue ops: throughput comparison + single memory bar."""
    df['workers'] = df['method'].str.extract(r'(\d+)').astype(int)
    avg = df.groupby('workers').mean(numeric_only=True).reset_index()
    avg = avg.sort_values('workers')
    ws  = avg['workers'].astype(str)
    x   = range(len(avg))
    w   = 0.22

    def _fmt_ops(v):
        if v >= 1e6:
            return f"{v/1e6:.1f}M"
        if v >= 1e3:
            return f"{v/1e3:.1f}K"
        return f"{v:.0f}"

    # 1. Throughput: 4 bars per worker count
    fig, ax = plt.subplots(figsize=(13, 7))
    b1 = ax.bar([i - 1.5*w for i in x], avg['conc_push_ops_per_sec'],
                w, label='Concurrent: push ops/s',     color='steelblue')
    b2 = ax.bar([i - 0.5*w for i in x], avg['conc_queue_ops_per_sec'],
                w, label='Concurrent: push→pop ops/s', color='cornflowerblue')
    b3 = ax.bar([i + 0.5*w for i in x], avg['iso_push_ops_per_sec'],
                w, label='Isolated: push ops/s',        color='darkorange')
    b4 = ax.bar([i + 1.5*w for i in x], avg['iso_pop_ops_per_sec'],
                w, label='Isolated: pop ops/s',          color='gold')
    for bars in (b1, b2, b3, b4):
        ax.bar_label(bars,
                     labels=[_fmt_ops(v) for v in [r.get_height() for r in bars]],
                     fontsize=7, rotation=90, padding=3)
    ax.set_xticks(list(x))
    ax.set_xticklabels(ws)
    ax.set_xlabel('Number of Workers')
    ax.set_ylabel('Operations per Second')
    ax.set_title(f'Push/Pop Throughput: Concurrent vs Isolated - {base_name}')
    ax.legend(fontsize=9)
    ax.grid(True, axis='y')
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'throughput_{base_name}.png'))
    plt.close()

    # 2. Memory: N×task_t in queue (peak_task_kb = mem_run_cost_kb here)
    fig2, ax2 = plt.subplots(figsize=(10, 5))
    mb = ax2.bar(ws, avg['mem_run_cost_kb'], color='steelblue')
    ax2.bar_label(mb,
                  labels=[f"{int(v)} KB" for v in avg['mem_run_cost_kb']],
                  fontsize=9, padding=3)
    ax2.set_xlabel('Number of Workers')
    ax2.set_ylabel('Peak task memory (KB)')
    ax2.set_title(f'Memory: N tasks in queue (N×sizeof(task_t)) - {base_name}')
    ax2.grid(True, axis='y')
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'memory_{base_name}.png'))
    plt.close()

    print(f"Queue-ops plots generated for {base_name} in {output_dir}")


def _plot_aging(df, base_name, output_dir):
    """
    Aging benchmark: aging_disabled vs aging_enabled.

    Charts:
      1. low_last_done_us  — key starvation metric (ms, lower is better)
      2. total_time_us     — throughput overhead of aging (ms)
      3. mem_run_cost_kb   — memory delta
    """
    avg = df.groupby('method').mean(numeric_only=True).reset_index()
    # Consistent order: disabled first, enabled second
    order = ['aging_disabled', 'aging_enabled']
    avg['method'] = pd.Categorical(avg['method'], categories=order, ordered=True)
    avg = avg.sort_values('method')
    methods = avg['method'].tolist()
    x = range(len(methods))
    colors = ['#e07070', '#6aaf6a']  # red = no aging (starvation), green = aging

    def _bar_labeled(ax, values, labels, color_list, ylabel, title, fmt=None):
        bars = ax.bar(x, values, color=color_list, width=0.5)
        ax.set_xticks(list(x))
        ax.set_xticklabels(labels)
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.grid(True, axis='y')
        for bar, v in zip(bars, values):
            lbl = fmt(v) if fmt else f"{v:.1f}"
            ax.text(bar.get_x() + bar.get_width() / 2,
                    bar.get_height() + max(values) * 0.01,
                    lbl, ha='center', va='bottom', fontsize=10, fontweight='bold')

    # ── 1. Low-priority starvation time ────────────────────────────────────
    fig, ax = plt.subplots(figsize=(8, 5))
    vals = (avg['low_last_done_us'] / 1000.0).tolist()   # ms
    _bar_labeled(ax, vals, methods, colors,
                 'Time until last LOW task done (ms)',
                 f'LOW Task Starvation — {base_name}\n'
                 f'(smaller = less starvation)',
                 fmt=lambda v: f"{v:.0f} ms")
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'starvation_{base_name}.png'))
    plt.close()

    # ── 2. Total time (throughput overhead of aging) ────────────────────────
    fig, ax = plt.subplots(figsize=(8, 5))
    vals = (avg['total_time_us'] / 1000.0).tolist()   # ms
    _bar_labeled(ax, vals, methods, colors,
                 'Total time (ms)',
                 f'Total Completion Time — {base_name}',
                 fmt=lambda v: f"{v:.0f} ms")
    plt.tight_layout()
    plt.savefig(os.path.join(output_dir, f'total_time_{base_name}.png'))
    plt.close()

    # ── 3. Memory ───────────────────────────────────────────────────────────
    if 'mem_run_cost_kb' in avg.columns:
        fig, ax = plt.subplots(figsize=(8, 5))
        vals = avg['mem_run_cost_kb'].tolist()
        _bar_labeled(ax, vals, methods, colors,
                     'mem_run_cost_kb (KB)',
                     f'Memory Cost While Running — {base_name}',
                     fmt=lambda v: f"{int(v)} KB")
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, f'memory_{base_name}.png'))
        plt.close()

    print(f"Aging plots generated for {base_name} in {output_dir}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python benchmark/plot/plot.py <csv_file_path>")
    else:
        plot_benchmark(sys.argv[1], "benchmark/plot/")
