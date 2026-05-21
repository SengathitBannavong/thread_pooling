#!/bin/bash
#
# Report-grade perf profiler for the thread pool vs the Pacheco baseline.
#
# For each benchmark it answers two report questions, mine vs baseline:
#   - "which FUNCTIONS eat the most time?"  -> *_top.txt / *_callgraph.txt (perf report)
#   - "which SYSCALLS eat the most time?"   -> *_syscalls.txt              (perf trace -s)
# plus hardware counters with mean +/- stddev -> *_stat.txt                (perf stat -r)
#
# Output goes to benchmark/perf/<label>/. Raw *.perf.data captures are kept for
# drill-down but are gitignored (large binaries).
#
# Usage:
#   make benchmarks benchmarks-base   # build the binaries first
#   bash perf_run.sh                  # or: make perf-report
#
# NOTE: perf trace (syscall view) and dwarf call-graphs need
#   kernel.perf_event_paranoid <= 1. If yours is higher, run:
#       sudo sysctl kernel.perf_event_paranoid=1
#   or run this script with sudo. The script degrades gracefully otherwise.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT" || exit 1

OUT_BASE="benchmark/perf"
STAT_EVENTS="cycles,instructions,cache-misses,cache-references,context-switches,cpu-migrations,page-faults,LLC-loads,LLC-load-misses"
STAT_REPEAT=5

# label | mine binary | base binary | args
TARGETS=(
    "queue_ops|./bin/benchmark_queue_ops|./bin/benchmark_base_queue_ops|1000000 3"
    "io|./bin/benchmark_io|./bin/benchmark_base_io|100000 1"
    "cpu|./bin/benchmark_cpu|./bin/benchmark_base_cpu|100000 1"
)

# ---- pre-flight ------------------------------------------------------------
if ! command -v perf >/dev/null 2>&1; then
    echo "ERROR: perf not found in PATH." >&2
    exit 1
fi

PARANOID="$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo unknown)"
ALLOW_TRACE=1
echo "perf_event_paranoid = $PARANOID"
if [[ "$PARANOID" =~ ^[0-9]+$ ]] && (( PARANOID > 1 )); then
    echo "WARNING: perf_event_paranoid=$PARANOID (> 1)."
    echo "         Syscall tracing (perf trace) and dwarf call-graphs may be denied."
    echo "         Run: sudo sysctl kernel.perf_event_paranoid=1   (or run with sudo)."
    echo "         Continuing; perf stat counters should still work."
    ALLOW_TRACE=0
fi

# ---- helpers ---------------------------------------------------------------
# profile <out_dir> <side> <binary> <args...>
profile() {
    local dir="$1"; shift
    local side="$1"; shift
    local bin="$1"; shift
    local args=("$@")

    if [[ ! -x "$bin" ]]; then
        echo "  SKIP $side: binary not found ($bin) -- build it first." >&2
        return
    fi

    echo "  [$side] $bin ${args[*]}"
    local data="$dir/${side}.perf.data"

    # --- function-level hotspots (record once, report twice) ---
    if perf record -F 999 -g --call-graph=dwarf -o "$data" -- "$bin" "${args[@]}" \
            >/dev/null 2>"$dir/${side}_record.log"; then
        perf report -i "$data" --stdio --no-children -n 2>/dev/null \
            | head -60 > "$dir/${side}_top.txt"
        perf report -i "$data" --stdio -g graph,0.5,caller 2>/dev/null \
            | head -120 > "$dir/${side}_callgraph.txt"
    else
        echo "    perf record failed (see ${side}_record.log)" >&2
    fi

    # --- syscall-level time summary ---
    if (( ALLOW_TRACE )); then
        if ! perf trace -s -o "$dir/${side}_syscalls.txt" -- "$bin" "${args[@]}" \
                >/dev/null 2>>"$dir/${side}_record.log"; then
            echo "    perf trace failed (paranoid/permissions) -- skipping syscall view" >&2
        fi
    else
        echo "  syscall view skipped: lower perf_event_paranoid to <= 1" \
            > "$dir/${side}_syscalls.txt"
    fi

    # --- hardware counters, mean +/- stddev over $STAT_REPEAT runs ---
    perf stat -r "$STAT_REPEAT" -e "$STAT_EVENTS" \
        -o "$dir/${side}_stat.txt" -- "$bin" "${args[@]}" >/dev/null 2>&1 \
        || echo "    perf stat failed for $side" >&2
}

# ---- run -------------------------------------------------------------------
for entry in "${TARGETS[@]}"; do
    IFS='|' read -r label mine base args <<< "$entry"
    read -ra arg_arr <<< "$args"

    dir="$OUT_BASE/$label"
    mkdir -p "$dir"
    echo "=== $label (args: $args) ==="
    profile "$dir" "mine" "$mine" "${arg_arr[@]}"
    profile "$dir" "base" "$base" "${arg_arr[@]}"
done

# ---- footer ----------------------------------------------------------------
echo
echo "Done. Outputs under $OUT_BASE/<label>/:"
echo "  *_top.txt        function self-time hotspots (what eats CPU time)"
echo "  *_callgraph.txt  caller tree for the hotspots"
echo "  *_syscalls.txt   per-syscall time summary (what eats wall time in the kernel)"
echo "  *_stat.txt       cycles/instructions/cache/context-switch counters (mean +/- stddev)"
echo "  *.perf.data      raw capture for 'perf report' drill-down (gitignored)"
