"""
bakery_plot.py
Parse Bakery Lock log and plot analysis
Compatible with log format:
  thread #X is waiting
  thread #X is done waiting
  thread #X on critical section
  thread #X done critical section
  thread #X is unlocking

Usage:
  python3 bakery_plot.py log.txt
"""

import sys
import re
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from collections import defaultdict

# ------------------------------------------------------------------ #
#  Parse                                                               #
# ------------------------------------------------------------------ #

def parse_log(filename):
    cs_entries  = defaultdict(int)
    cs_order    = []
    wait_start  = {}   # tid -> line number
    wait_events = []   # {thread, duration}

    with open(filename) as f:
        lines = f.readlines()

    for lineno, line in enumerate(lines):
        line = line.strip()

        m = re.match(r"thread #(\d+) is waiting", line)
        if m:
            tid = int(m.group(1))
            wait_start[tid] = lineno

        m = re.match(r"thread #(\d+) is done waiting", line)
        if m:
            tid = int(m.group(1))
            if tid in wait_start:
                wait_events.append({
                    "thread":   tid,
                    "duration": lineno - wait_start.pop(tid)
                })

        m = re.match(r"thread #(\d+) on critical section", line)
        if m:
            tid = int(m.group(1))
            cs_entries[tid] += 1
            cs_order.append(tid)

    return cs_entries, cs_order, wait_events

# ------------------------------------------------------------------ #
#  Plot                                                                #
# ------------------------------------------------------------------ #

def plot(filename):
    cs_entries, cs_order, wait_events = parse_log(filename)

    threads = sorted(cs_entries.keys())
    colors  = plt.cm.tab10.colors
    counts  = [cs_entries[t] for t in threads]
    ideal   = sum(counts) / len(threads)
    fairness = min(counts) / max(counts) if max(counts) > 0 else 1.0

    # find starved threads (below 50% of ideal)
    starved = [t for t in threads if cs_entries[t] < ideal * 0.5]

    fig = plt.figure(figsize=(18, 14))
    fig.suptitle(f"Bakery Lock Analysis — {filename}\n"
                 f"Fairness ratio: {fairness:.3f}  |  "
                 f"Starved threads: {[f'T{t}' for t in starved] or 'None'}",
                 fontsize=13, fontweight="bold")

    gs = gridspec.GridSpec(3, 2, hspace=0.55, wspace=0.35,
                           height_ratios=[1, 1, 1])

    # ---- 1. CS entry count -------------------------------------------
    ax1 = fig.add_subplot(gs[0, 0])
    bars = ax1.bar(
        [f"T{t}" for t in threads], counts,
        color=[colors[t % 10] for t in threads],
        edgecolor="black", linewidth=0.8
    )
    ax1.axhline(ideal, color="green", linestyle="--",
                linewidth=1.5, label=f"Ideal ({ideal:.1f})")
    ax1.set_title("CS Entry Count per Thread")
    ax1.set_xlabel("Thread")
    ax1.set_ylabel("Count")
    ax1.legend(fontsize=8)
    for bar, val in zip(bars, counts):
        ax1.text(bar.get_x() + bar.get_width()/2,
                 bar.get_height() + 0.3,
                 str(val), ha="center", va="bottom",
                 fontsize=10, fontweight="bold")
    # highlight starved threads
    for i, t in enumerate(threads):
        if t in starved:
            bars[i].set_edgecolor("red")
            bars[i].set_linewidth(3)
    ax1.set_ylim(0, max(counts) * 1.3)

    # ---- 2. Wait duration box plot -----------------------------------
    ax2 = fig.add_subplot(gs[0, 1])
    wait_by_thread = defaultdict(list)
    for ev in wait_events:
        wait_by_thread[ev["thread"]].append(ev["duration"])

    data_box   = [wait_by_thread.get(t, [0]) for t in threads]
    labels_box = [f"T{t}" for t in threads]
    bp = ax2.boxplot(data_box, labels=labels_box, patch_artist=True)
    for patch, t in zip(bp["boxes"], threads):
        patch.set_facecolor(colors[t % 10])
        patch.set_alpha(0.7)
    ax2.set_title("Wait Duration Distribution\n(log lines = proxy for time)")
    ax2.set_xlabel("Thread")
    ax2.set_ylabel("Duration (log lines)")

    # ---- 3. CS entry order timeline ----------------------------------
    ax3 = fig.add_subplot(gs[1, 0])
    for idx, tid in enumerate(cs_order):
        ax3.scatter(idx, tid,
                    color=colors[tid % 10], s=35, zorder=3)
    ax3.set_title("CS Entry Order Timeline\n(gap in row = not getting CS)")
    ax3.set_xlabel("CS entry sequence")
    ax3.set_ylabel("Thread ID")
    ax3.set_yticks(threads)
    ax3.set_yticklabels([f"T{t}" for t in threads])
    ax3.grid(axis="y", linestyle="--", alpha=0.5)

    # mark first appearance of starved threads
    for t in starved:
        first = next((i for i, x in enumerate(cs_order) if x == t), None)
        if first is not None:
            ax3.axvline(first, color="red", linestyle="--",
                        linewidth=1.5, alpha=0.7)
            ax3.annotate(f"T{t} first\n@ {first}",
                         xy=(first, t),
                         xytext=(first - len(cs_order)*0.15, t - 0.4),
                         arrowprops=dict(arrowstyle="->", color="red"),
                         color="red", fontsize=7)

    # ---- 4. Cumulative CS entries ------------------------------------
    ax4 = fig.add_subplot(gs[1, 1])
    running    = defaultdict(int)
    cumulative = defaultdict(list)
    for idx, tid in enumerate(cs_order):
        running[tid] += 1
        for t in threads:
            cumulative[t].append(running[t])

    for t in threads:
        lw = 2.5 if t in starved else 1.5
        ls = "--" if t in starved else "-"
        ax4.plot(range(len(cs_order)), cumulative[t],
                 label=f"T{t}", color=colors[t % 10],
                 linewidth=lw, linestyle=ls)
    ax4.set_title("Cumulative CS Entries\n(flat = starvation period)")
    ax4.set_xlabel("CS entry sequence")
    ax4.set_ylabel("Cumulative count")
    ax4.legend(loc="upper left", fontsize=8)
    ax4.grid(linestyle="--", alpha=0.4)

    # ---- 5. Deviation from ideal (full width) ------------------------
    ax5 = fig.add_subplot(gs[2, :])
    deviation = [cs_entries[t] - ideal for t in threads]
    bar_colors = []
    for t, dev in zip(threads, deviation):
        if dev >= 0:
            bar_colors.append(colors[t % 10])
        else:
            bar_colors.append("red")

    bars5 = ax5.bar(
        [f"T{t}" for t in threads], deviation,
        color=bar_colors, edgecolor="black", linewidth=0.8, alpha=0.85
    )
    ax5.axhline(0, color="green", linewidth=2,
                linestyle="--", label=f"Ideal ({ideal:.1f} CS each)")
    ax5.set_title("Deviation from Ideal CS Count\n"
                  "(red bar below 0 = got fewer than fair share → starvation)",
                  fontsize=10)
    ax5.set_xlabel("Thread")
    ax5.set_ylabel("Deviation (CS count − ideal)")
    ax5.legend(fontsize=9)

    for bar, val in zip(bars5, deviation):
        ypos = bar.get_height() + 0.2 if val >= 0 else bar.get_height() - 0.8
        ax5.text(bar.get_x() + bar.get_width()/2, ypos,
                 f"{val:+.1f}", ha="center", va="bottom",
                 fontsize=9, fontweight="bold",
                 color="red" if val < 0 else "black")

    # ---- Summary text ------------------------------------------------
    t2_first = {t: next((i for i,x in enumerate(cs_order) if x==t), None)
                for t in starved}
    summary_parts = [
        f"Total CS: {sum(counts)}",
        f"Fairness: {fairness:.3f}",
    ]
    for t in threads:
        summary_parts.append(f"T{t}={counts[t]}")
    if starved:
        for t, idx in t2_first.items():
            summary_parts.append(
                f"T{t} first CS @ seq {idx}/{len(cs_order)}"
            )

    fig.text(0.5, 0.005, "  |  ".join(summary_parts),
             ha="center", fontsize=9,
             bbox=dict(boxstyle="round", facecolor="lightyellow", alpha=0.8))

    out = filename.replace(".txt", "_bakery_analysis.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved: {out}")
    plt.show()

# ------------------------------------------------------------------ #
#  Main                                                                #
# ------------------------------------------------------------------ #

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 bakery_plot.py <log_file>")
        sys.exit(1)
    plot(sys.argv[1])