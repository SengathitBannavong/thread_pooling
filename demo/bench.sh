#!/usr/bin/env bash
#
# demo/bench.sh — load-test + latency evaluator for the demo HTTP server.
#
# This does NOT start or stop any server. You run the server yourself (in pool
# OR naive mode, your choice); this script just fires N concurrent requests at a
# target URL and reports throughput + latency percentiles. Run it once against
# your pool server and once against your naive server and compare the two.
#
# Usage:
#   demo/bench.sh [options]
#
#   --url URL          target URL or path to hit
#                      [http://127.0.0.1:8080/render?w=480&h=480&iter=1200]
#                      (a bare /path is joined onto --host/--port)
#   --host HOST        host used when --url is a bare path        [127.0.0.1]
#   --port PORT        port used when --url is a bare path        [8080]
#   --requests N / -n  total requests                            [2000]
#   --concurrency C/-c concurrent in-flight requests             [200]
#   --pid PID          server PID to sample peak OS threads (optional)
#   -h | --help        show this help
#
# Examples:
#   # terminal 1: ./bin/http_server --mode pool  --no-monitor --port 8080
#   # terminal 2:
#   demo/bench.sh                                            # hit the default URL
#   demo/bench.sh --url '/render?w=800&h=800&iter=2000' -c 300
#   demo/bench.sh --pid "$(pgrep -f 'http_server --mode pool')"   # also sample threads
#
set -euo pipefail

# ── defaults ───────────────────────────────────────────────────────────────
URL='/render?w=480&h=480&iter=1200'
# HOST='127.0.0.1'
HOST='chadolfjerry.page/threadpool'
PORT=8080
REQUESTS=2000
CONCURRENCY=200
SAMPLE_PID=''

# ── parse args ─────────────────────────────────────────────────────────────
while [ $# -gt 0 ]; do
        case "$1" in
        --url)             URL="$2"; shift 2 ;;
        --host)            HOST="$2"; shift 2 ;;
        --port)            PORT="$2"; shift 2 ;;
        --requests|-n)     REQUESTS="$2"; shift 2 ;;
        --concurrency|-c)  CONCURRENCY="$2"; shift 2 ;;
        --pid)             SAMPLE_PID="$2"; shift 2 ;;
        -h|--help)         grep -E '^#( |$)' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
        esac
done

command -v curl >/dev/null || { echo "error: curl is required" >&2; exit 1; }

# resolve target: full URL as-is, bare /path joined onto host:port
# case "$URL" in
#         http://*|https://*) TARGET="$URL" ;;
#         *)                  TARGET="http://${HOST}:${PORT}${URL}" ;;
# esac

case "$URL" in
        http://*|https://*) TARGET="$URL" ;;
        *)                  TARGET="https://${HOST}${URL}" ;;
esac

# ── preflight: is the server actually up? ──────────────────────────────────
if ! curl -s -o /dev/null --max-time 3 "$TARGET"; then
        echo "error: no response from $TARGET" >&2
        echo "       start the server first, e.g.:" >&2
        echo "       ./bin/http_server --mode pool --no-monitor --port ${PORT}" >&2
        exit 1
fi

TMP="$(mktemp -d)"
SAMPLER=''
cleanup() {
        [ -n "$SAMPLER" ] && kill "$SAMPLER" 2>/dev/null || true
        rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

# sample max OS thread count of $1 into $2, refreshed each tick, until killed
sample_threads() {
        local pid="$1" out="$2" max=0 th
        echo 0 > "$out"
        while kill -0 "$pid" 2>/dev/null; do
                th="$(ps -o nlwp= -p "$pid" 2>/dev/null | tr -d ' ')"
                if [ -n "$th" ] && [ "$th" -gt "$max" ]; then
                        max="$th"; echo "$max" > "$out"
                fi
                sleep 0.03
        done
}

echo "════════════════════════════════════════════════════════════════"
echo " Target : $TARGET"
echo " Load   : $REQUESTS requests, concurrency $CONCURRENCY"
[ -n "$SAMPLE_PID" ] && echo " Sample : threads of pid $SAMPLE_PID"
echo "════════════════════════════════════════════════════════════════"

# ── optional thread sampler ────────────────────────────────────────────────
THF="$TMP/threads"
if [ -n "$SAMPLE_PID" ]; then
        if kill -0 "$SAMPLE_PID" 2>/dev/null; then
                sample_threads "$SAMPLE_PID" "$THF" &
                SAMPLER=$!
        else
                echo "warning: pid $SAMPLE_PID not alive; skipping thread sampling" >&2
                SAMPLE_PID=''
        fi
fi

# ── drive the load ─────────────────────────────────────────────────────────
RES="$TMP/res"
echo "[bench] running…"
t0="$(date +%s.%N)"
seq 1 "$REQUESTS" | xargs -P "$CONCURRENCY" -I{} \
        curl -s -o /dev/null -w '%{http_code} %{time_total}\n' "$TARGET" \
        >"$RES" 2>/dev/null || true
t1="$(date +%s.%N)"
wall="$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.3f", b-a}')"

if [ -n "$SAMPLER" ]; then
        kill "$SAMPLER" 2>/dev/null || true
        wait "$SAMPLER" 2>/dev/null || true
        SAMPLER=''
fi

# ── compute stats ──────────────────────────────────────────────────────────
ok="$(awk '$1==200{c++} END{print c+0}' "$RES")"
fail="$(awk -v n="$REQUESTS" '$1==200{c++} END{print n-(c+0)}' "$RES")"
stats="$(awk '{print $2*1000}' "$RES" | sort -n | awk '
        function pc(p,  i){i=int((p/100.0)*(n-1))+1; if(i<1)i=1; if(i>n)i=n; return v[i]}
        {v[NR]=$1; s+=$1}
        END{
                n=NR; if(n==0){print "0 0 0 0 0 0"; exit}
                printf "%.2f %.2f %.2f %.2f %.2f %.2f", s/n, v[1], v[n], pc(50), pc(90), pc(99)
        }')"
read -r avg min max p50 p90 p99 <<<"$stats"
rps="$(awk -v n="$REQUESTS" -v w="$wall" 'BEGIN{printf "%.1f", (w>0)?n/w:0}')"

# ── report ─────────────────────────────────────────────────────────────────
echo
printf '  wall time     : %s s\n' "$wall"
printf '  throughput    : %s req/s\n' "$rps"
printf '  requests      : %s ok, %s failed (of %s)\n' "$ok" "$fail" "$REQUESTS"
printf '  latency (ms)  : avg %s | p50 %s | p90 %s | p99 %s | min %s | max %s\n' \
        "$avg" "$p50" "$p90" "$p99" "$min" "$max"
if [ -n "$SAMPLE_PID" ]; then
        printf '  peak threads  : %s (server pid %s)\n' "$(cat "$THF" 2>/dev/null || echo '?')" "$SAMPLE_PID"
fi
echo
[ "$fail" -gt 0 ] && echo "note: ${fail} request(s) failed — under heavy load that often means the" \
        "server (esp. naive mode) hit a resource limit; that is itself a result."
exit 0
