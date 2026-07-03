#!/usr/bin/env bash
#
# demo/bench_latency.sh — concurrency-sweep tail-latency collector.
#
# Unlike demo/bench.sh (single config, prints percentiles then throws the raw
# samples away), this drives a sweep of concurrency levels against ONE running
# server and KEEPS every per-request latency to disk, so percentiles (incl p95)
# and a latency CDF can be computed/plotted afterwards by latency_stats.py.
#
# It does NOT start a server. Point it at a live endpoint (local or the VPS).
#
# Usage:
#   demo/bench_latency.sh [--url URL] [--requests N] [--concurrency "1 2 4 8 ..."]
#                         [--outdir DIR] [--label NAME]
#
# Output (in --outdir, default demo/bench_results):
#   raw_<label>_c<C>.txt   # one line per request: "<http_code> <time_total_s>"
#   meta_<label>.txt       # run metadata (url, params, wall time per config)
set -euo pipefail

URL='https://chadolfjerry.page/threadpool/render?w=320&h=320&iter=600'
REQUESTS=120
CONC_LIST="1 2 4 8 16 32 64"
OUTDIR='demo/bench_results'
LABEL='vps_pool'

while [ $# -gt 0 ]; do
        case "$1" in
        --url)         URL="$2"; shift 2 ;;
        --requests|-n) REQUESTS="$2"; shift 2 ;;
        --concurrency) CONC_LIST="$2"; shift 2 ;;
        --outdir)      OUTDIR="$2"; shift 2 ;;
        --label)       LABEL="$2"; shift 2 ;;
        -h|--help)     grep -E '^#( |$)' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
        esac
done

command -v curl >/dev/null || { echo "error: curl required" >&2; exit 1; }
mkdir -p "$OUTDIR"

# preflight
if ! curl -s -o /dev/null --max-time 25 "$URL"; then
        echo "error: no response from $URL" >&2; exit 1
fi

META="$OUTDIR/meta_${LABEL}.txt"
{
        echo "url=$URL"
        echo "requests_per_config=$REQUESTS"
        echo "concurrency_list=$CONC_LIST"
        echo "started=$(date -u +%FT%TZ)"
        echo "client_host=$(hostname)"
} > "$META"

echo "════════════════════════════════════════════════════════════════"
echo " Target : $URL"
echo " Sweep  : concurrency [$CONC_LIST], $REQUESTS requests each"
echo " Out    : $OUTDIR (label=$LABEL)"
echo "════════════════════════════════════════════════════════════════"

for C in $CONC_LIST; do
        RAW="$OUTDIR/raw_${LABEL}_c${C}.txt"
        echo "[bench] concurrency=$C → $RAW"
        t0="$(date +%s.%N)"
        seq 1 "$REQUESTS" | xargs -P "$C" -I{} \
                curl -s -o /dev/null -w '%{http_code} %{time_total}\n' --max-time 60 "$URL" \
                > "$RAW" 2>/dev/null || true
        t1="$(date +%s.%N)"
        wall="$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.3f", b-a}')"
        ok="$(awk '$1==200{c++} END{print c+0}' "$RAW")"
        echo "         done: ${ok}/${REQUESTS} ok in ${wall}s"
        echo "config c=$C wall=$wall ok=$ok" >> "$META"
        sleep 3   # let the 2 vCPU box recover between configs
done

echo "finished=$(date -u +%FT%TZ)" >> "$META"
echo "[bench] raw data in $OUTDIR"
