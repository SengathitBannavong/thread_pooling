"""
filter_lock_plot.py
Parse Filter Lock log file and plot:
  1. CS entry count per thread (bar chart)
  2. Wait duration distribution (box plot)
  3. CS entry order timeline (scatter)
  4. Cumulative CS entries over time (line)
  5. Level heatmap — how long each thread stuck at each level

Usage:
  python3 filter_lock_plot.py log.txt
"""

import sys
import re
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import matplotlib.colors as mcolors
from collections import defaultdict

# ------------------------------------------------------------------ #
#  Parse log                                                           #
# ------------------------------------------------------------------ #

def parse_log(filename):
    cs_entries   = defaultdict(int)
    cs_order     = []
    wait_start   = {}   # (thread, level) -> line_number
    wait_events  = []
    level_events = []

    with open(filename, "r") as f:
        lines = f.readlines()

    for lineno, line in enumerate(lines):
        line = line.strip()

        # thread #X is on level #Y
        m = re.match(r"thread #(\d+) is on level #(\d+)", line)
        if m:
            tid, lvl = int(m.group(1)), int(m.group(2))
            level_events.append({"thread": tid, "level": lvl, "line": lineno})

        # thread #X is waiting
        m = re.match(r"thread #(\d+) is waiting", line)
        if m:
            tid = int(m.group(1))
            lvl = None
            for ev in reversed(level_events):
                if ev["thread"] == tid:
                    lvl = ev["level"]
                    break
            if lvl is not None:
                wait_start[(tid, lvl)] = lineno

        # thread #X is done waiting
        m = re.match(r"thread #(\d+) is done waiting", line)
        if m:
            tid = int(m.group(1))
            lvl = None
            for ev in reversed(level_events):
                if ev["thread"] == tid:
                    lvl = ev["level"]
                    break
            if lvl is not None and (tid, lvl) in wait_start:
                start = wait_start.pop((tid, lvl))
                wait_events.append({
                    "thread":   tid,
                    "level":    lvl,
                    "start":    start,
                    "end":      lineno,
                    "duration": lineno - start
                })

        # thread #X on critical section
        m = re.match(r"thread #(\d+) on critical section", line)
        if m:
            tid = int(m.group(1))
            cs_entries[tid] += 1
            cs_order.append(tid)

    return cs_entries, wait_events, cs_order, level_events


# ------------------------------------------------------------------ #
#  Plot                                                                #
# ------------------------------------------------------------------ #

def plot(filename):
    cs_entries, wait_events, cs_order, level_events = parse_log(filename)

    threads = sorted(cs_entries.keys())
    colors  = plt.cm.tab10.colors

    fig = plt.figure(figsize=(18, 15))
    fig.suptitle(f"Filter Lock Analysis — {filename}", fontsize=14, fontweight="bold")

    # 2x2 grid + full-width heatmap at bottom
    gs = gridspec.GridSpec(3, 2, figure=fig, hspace=0.55, wspace=0.35,
                           height_ratios=[1, 1, 1.2])

    # ---- 1. CS entry count per thread --------------------------------
    ax1 = fig.add_subplot(gs[0, 0])
    counts = [cs_entries[t] for t in threads]
    bars = ax1.bar(
        [f"T{t}" for t in threads], counts,
        color=[colors[t % 10] for t in threads],
        edgecolor="black", linewidth=0.7
    )
    ax1.set_title("CS Entry Count per Thread")
    ax1.set_xlabel("Thread")
    ax1.set_ylabel("Count")
    for bar, val in zip(bars, counts):
        ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1,
                 str(val), ha="center", va="bottom", fontsize=9)
    ax1.set_ylim(0, max(counts) * 1.25)

    # ---- 2. Wait duration distribution (box plot) --------------------
    ax2 = fig.add_subplot(gs[0, 1])
    wait_by_thread = defaultdict(list)
    for ev in wait_events:
        wait_by_thread[ev["thread"]].append(ev["duration"])

    data_box   = [wait_by_thread[t] for t in threads]
    labels_box = [f"T{t}" for t in threads]
    bp = ax2.boxplot(data_box, labels=labels_box, patch_artist=True)
    for patch, t in zip(bp["boxes"], threads):
        patch.set_facecolor(colors[t % 10])
        patch.set_alpha(0.7)
    ax2.set_title("Wait Duration Distribution per Thread\n(log lines = proxy for time)")
    ax2.set_xlabel("Thread")
    ax2.set_ylabel("Duration (log lines)")

    # ---- 3. CS entry order timeline ----------------------------------
    ax3 = fig.add_subplot(gs[1, 0])
    for idx, tid in enumerate(cs_order):
        ax3.scatter(idx, tid, color=colors[tid % 10], s=60, zorder=3)
    ax3.set_title("CS Entry Order Timeline\n(gap in row = starvation period)")
    ax3.set_xlabel("CS entry sequence")
    ax3.set_ylabel("Thread ID")
    ax3.set_yticks(threads)
    ax3.set_yticklabels([f"T{t}" for t in threads])
    ax3.grid(axis="y", linestyle="--", alpha=0.5)

    # ---- 4. Cumulative CS entries ------------------------------------
    ax4 = fig.add_subplot(gs[1, 1])
    cumulative = defaultdict(list)
    running    = defaultdict(int)
    for idx, tid in enumerate(cs_order):
        running[tid] += 1
        for t in threads:
            cumulative[t].append(running[t])

    for t in threads:
        ax4.plot(range(len(cs_order)), cumulative[t],
                 label=f"T{t}", color=colors[t % 10], linewidth=1.8)
    ax4.set_title("Cumulative CS Entries\n(flat segment = starvation)")
    ax4.set_xlabel("CS entry sequence")
    ax4.set_ylabel("Cumulative count")
    ax4.legend(loc="upper left", fontsize=8)
    ax4.grid(linestyle="--", alpha=0.4)

    # ---- 5. Level Heatmap (full width) --------------------------------
    ax5 = fig.add_subplot(gs[2, :])

    max_level  = max(ev["level"] for ev in wait_events) if wait_events else 1
    n_threads  = len(threads)

    # accumulate total wait duration and count per (thread, level)
    wait_total = defaultdict(int)
    wait_count = defaultdict(int)
    for ev in wait_events:
        wait_total[(ev["thread"], ev["level"])] += ev["duration"]
        wait_count[(ev["thread"], ev["level"])] += 1

    # build matrix rows=threads, cols=levels
    matrix = np.zeros((n_threads, max_level))
    for row, tid in enumerate(threads):
        for col in range(max_level):
            matrix[row, col] = wait_total.get((tid, col + 1), 0)

    im = ax5.imshow(
        matrix, aspect="auto", cmap="YlOrRd",
        norm=mcolors.PowerNorm(gamma=0.5, vmin=0, vmax=max(matrix.max(), 1))
    )

    ax5.set_xticks(range(max_level))
    ax5.set_xticklabels([f"Level {l+1}" for l in range(max_level)], fontsize=9)
    ax5.set_yticks(range(n_threads))
    ax5.set_yticklabels([f"T{t}" for t in threads], fontsize=9)
    ax5.set_title(
        "Level Heatmap — Total Wait Duration per Thread per Level\n"
        "(darker red = stuck longer  |  each cell shows total_lines (N waits))",
        fontsize=10
    )
    ax5.set_xlabel("Level")
    ax5.set_ylabel("Thread")

    # annotate cells
    for row in range(n_threads):
        for col in range(max_level):
            tid  = threads[row]
            lvl  = col + 1
            tot  = wait_total.get((tid, lvl), 0)
            cnt  = wait_count.get((tid, lvl), 0)
            label = f"{tot}\n({cnt}x)" if cnt > 0 else "0"
            text_color = "white" if matrix[row, col] > matrix.max() * 0.55 else "black"
            ax5.text(col, row, label,
                     ha="center", va="center", fontsize=8, color=text_color)

    cbar = plt.colorbar(im, ax=ax5, orientation="vertical", pad=0.01, shrink=0.85)
    cbar.set_label("Total wait (log lines)", fontsize=9)

    # ---- Summary -----------------------------------------------------
    total_cs = sum(cs_entries.values())
    fairness = min(counts) / max(counts) if max(counts) > 0 else 1.0
    fig.text(
        0.5, 0.002,
        f"Total CS entries: {total_cs}  |  "
        f"Min: {min(counts)} (T{threads[counts.index(min(counts))]})  |  "
        f"Max: {max(counts)} (T{threads[counts.index(max(counts))]})  |  "
        f"Fairness ratio: {fairness:.2f}",
        ha="center", fontsize=9,
        bbox=dict(boxstyle="round", facecolor="lightyellow", alpha=0.8)
    )

    plt.savefig("filter_lock_analysis.png", dpi=150, bbox_inches="tight")
    print("Saved: filter_lock_analysis.png")
    plt.show()


# ------------------------------------------------------------------ #
#  Main                                                                #
# ------------------------------------------------------------------ #

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 filter_lock_plot.py <log_file>")
        sys.exit(1)
    plot(sys.argv[1])