# Tail-latency benchmark — how to run, plot, and get the raw data

This documents the **p50/p90/p95/p99 latency** workflow for the demo HTTP render
server (the synthetic C microbenchmarks under `benchmark/` are a separate suite —
see the bottom of this file). Three small tools, run in order:

| Step | Tool | What it does |
|------|------|--------------|
| 1. run   | `demo/bench_latency.sh` | fires a concurrency sweep, **saves raw per-request latency** |
| 2. calc  | `demo/latency_stats.py` | nearest-rank percentiles (incl **p95**) → summary CSV |
| 3. plot  | `demo/plot_latency.py`  | percentiles / throughput / CDF figures |

## Prerequisites

- `curl` (the load driver)
- `bash`, `python3` with `pandas` + `matplotlib` (for plotting)
- A reachable target: either the deployed VPS endpoint, **or** a local server
  (see "Run against a local server" below).

## 1. Run the sweep → raw data

`bench_latency.sh` does **not** start a server. Point it at a live URL; it fires
`--requests` requests at each concurrency level in `--concurrency`, and writes one
line per request (`<http_code> <time_total_seconds>`) to a raw file per level.

```bash
# against the deployed VPS (default target)
demo/bench_latency.sh

# explicit / custom
demo/bench_latency.sh \
  --url 'https://chadolfjerry.page/threadpool/render?w=320&h=320&iter=600' \
  --requests 120 \
  --concurrency "1 2 4 8 16 32 64" \
  --label vps_pool \
  --outdir demo/bench_results
```

Options: `--url URL`, `--requests N` (`-n`), `--concurrency "LIST"`,
`--label NAME`, `--outdir DIR`. There is a 3 s pause between levels so a small box
can recover.

### Where the raw data lands (this is "the raw")

```
demo/bench_results/
  raw_<label>_c<C>.txt          # one line per request: "200 0.025508"
  meta_<label>.txt              # url, params, wall time + ok count per level
```

`time_total` is in **seconds**; the stats tool converts to ms. Keep these files —
they are the source of truth; everything else is derived from them.

## 2. Compute percentiles → summary CSV

```bash
python3 demo/latency_stats.py --dir demo/bench_results --label vps_pool
```

Prints a per-concurrency table and writes
`demo/bench_results/latency_summary_<label>.csv` with columns:

```
concurrency,total,ok,fail,throughput_rps,avg_ms,min_ms,max_ms,stddev_ms,
p50_ms,p90_ms,p95_ms,p99_ms,p999_ms
```

Percentiles use the **nearest-rank** method over the successful (HTTP 200)
requests only; failures are counted separately. Throughput (`req/s`) is
`ok / wall_time` from the meta file.

Example output:

```
 conc    ok  fail     rps      avg      p50      p90      p95      p99
   16  1000     0   445.0     29.8     28.6     33.8     37.2     46.0
   32  1000     0   467.5     49.6     48.5     62.1     67.5     82.0
   64  1000     0   475.7     56.2     51.4     88.9     96.1    107.7
```

## 3. Plot

```bash
python3 demo/plot_latency.py --dir demo/bench_results --label vps_pool \
        --outdir benchmark/plot/latency
```

Writes three PNGs to `benchmark/plot/latency/`:

- `percentiles_<label>.png` — p50/p90/p95/p99 vs concurrency
- `throughput_<label>.png`  — achieved req/s vs concurrency
- `cdf_<label>.png`         — latency CDF for low / mid / high concurrency

## Run against a local server (pure server-side latency)

The VPS measurement is **end-to-end over the internet**, so absolute latency
includes network RTT/TLS. To measure pure server cost, run against a local
server. (Caveat: client and server then share the same CPUs, so at very high
concurrency the curl clients contend with the workers.)

```bash
# build + start the server (pool mode, no ncurses monitor)
make demo
./bin/http_server --mode pool --no-monitor --host 127.0.0.1 --port 8080 &

# sweep it, with a distinct label
demo/bench_latency.sh \
  --url 'http://127.0.0.1:8080/render?w=320&h=320&iter=600' \
  --label local_pool --requests 1000

python3 demo/latency_stats.py --label local_pool
python3 demo/plot_latency.py  --label local_pool

# stop the server (use -x so it matches the process name, NOT your shell)
pkill -x http_server
```

To contrast the pool against a thread-per-request server, start a second server
with `--mode naive` on another port and sweep that with `--label naive`.

### Tips

- **Sample size vs tail:** for stable p99 a config needs enough requests. Locally
  a render is ~25 ms, so 120 requests finish in <0.3 s and the high-concurrency
  tail is noisy — use `--requests 1000+` for clean percentiles. The VPS is slow
  enough that 120 is adequate.
- **Don't use `pkill -f http_server`** from a shell whose command line contains
  that string — it matches and kills its own shell. Use `pkill -x http_server`.

## Reproducing a full run

```bash
make demo
./bin/http_server --mode pool --no-monitor --port 8080 &
demo/bench_latency.sh --url 'http://127.0.0.1:8080/render?w=320&h=320&iter=600' --label local_pool -n 1000
python3 demo/latency_stats.py --label local_pool
python3 demo/plot_latency.py  --label local_pool
pkill -x http_server
```

---

## Appendix: the synthetic C benchmark suite (`benchmark/`)

Separate from the latency tools above. These measure throughput / scaling /
memory of the thread-pool library itself (no HTTP). They do **not** measure tail
latency — their `avg_latency_us` column is just `total_time_us / n_tasks`.

```bash
make run-benchmarks        # mine: cpu io scaling queue aging hetero stability
make run-benchmarks-base   # baseline (FIFO) pool, same workloads
bash run_plot.sh           # raw CSVs -> PNGs in benchmark/plot/<bucket>/
python parse_to_latex.py   # comparative tables -> benchmark/res/tables.tex
```

Raw CSVs live in `benchmark/res/result_*.txt`; see `benchmark/res/MANIFEST.txt`
for the machine, exact commands, and headline numbers of the packaged run.
