# Thread-Pool HTTP Server — Demo

An application-layer demo of the thread pool: a tiny from-scratch HTTP/1.0 server
(raw POSIX sockets, zero dependencies beyond what the pool already uses) that
serves two kinds of work through one pool:

- **`/` + static assets** — IO route. Serves a control-panel web page from a docroot.
- **`/render?w=&h=&iter=`** — CPU route. Computes a Mandelbrot set and returns it
  as a BMP image (browsers render BMP).

The page is also the **load generator**: a "heaviness" slider plus *Fire 1* / *Fire N*
buttons issue concurrent `/render` requests so you can saturate the pool on demand
and watch the live ncurses monitor.

## Build

```bash
make demo            # produces bin/http_server
```

> Run the server **from the repository root** — the default docroot is the
> relative path `demo/www`. (Or pass an absolute `--docroot`.)

## Quick start

```bash
make demo
./bin/http_server                       # pool mode, monitor ON, http://127.0.0.1:8080
```

Then open <http://127.0.0.1:8080> in a browser, drag the **Heaviness** slider up,
and click **Fire N**. Watch the ncurses monitor (in the terminal running the
server) light up workers and grow the queue while the worker count stays fixed.

Press **Ctrl-C** to shut down: the monitor detaches (terminal restored), in-flight
requests drain, then the process exits cleanly.

## Flags

All flags are optional; defaults shown in brackets.

| Flag | Default | Meaning |
|------|---------|---------|
| `--port N` | `8080` | TCP port to listen on (1–65535). |
| `--workers N` | number of CPU cores | Worker threads in the pool (pool mode only). |
| `--mode pool\|naive` | `pool` | `pool` = use the thread pool. `naive` = spawn one detached pthread **per connection** (the contrast case). |
| `--docroot PATH` | `demo/www` | Directory served for static files. Must exist (resolved at startup). |
| `--host ADDR` | `127.0.0.1` | Interface to bind. Use `0.0.0.0` to reach it from another machine. |
| `--no-monitor` | monitor ON | Disable the ncurses monitor. Required for headless / scripted / piped runs. |

Notes:
- The ncurses **monitor only attaches in `pool` mode**. In `naive` mode there is no
  pool to monitor.
- While the monitor is attached the server writes nothing to stdout (so it can't
  corrupt the TUI). Run with `--no-monitor` when you want plain logging or to drive
  it from scripts.

## Recipes

**Default live demo (pool + monitor):**
```bash
./bin/http_server
# browser: http://127.0.0.1:8080  -> slider + Fire N
```

**More workers, custom port:**
```bash
./bin/http_server --workers 8 --port 9000
```

**Headless (for curl / ab / wrk / CI — no TUI):**
```bash
./bin/http_server --no-monitor --port 8137 &
curl -s -o out.bmp "http://127.0.0.1:8137/render?w=512&h=512&iter=1000"
curl -s            "http://127.0.0.1:8137/"
```

**Bounded-concurrency contrast (the money shot):**

Terminal 1 — pool mode, watch threads stay fixed:
```bash
./bin/http_server --no-monitor --port 8137 &
ps -T -p $!        # or: htop -p $!   -> worker count stays at N under load
```
Terminal 2 — hammer it:
```bash
for i in $(seq 1 200); do
  curl -s -o /dev/null "http://127.0.0.1:8137/render?w=512&h=512&iter=1500" &
done; wait
```
Now repeat with `--mode naive` and watch the thread count **explode** under the
same load (one thread per in-flight connection) instead of staying bounded.

**Expose to the LAN (e.g. demo from a phone):**
```bash
./bin/http_server --host 0.0.0.0 --port 8080
# then browse to http://<this-machine-ip>:8080
```
> The HTTP parser is intentionally minimal; only expose it on a trusted network.

## Live monitor: detach / reattach + request log

In pool mode with the monitor on, you can toggle the ncurses dashboard at runtime.
**When the monitor is detached, the terminal is restored and the server streams a
one-line log per client request; when it is reattached, the dashboard returns and
logging is suppressed** (so it can never corrupt the TUI).

Keys:

| State | Key | Effect |
|-------|-----|--------|
| Dashboard (attached) | `q` / `Q` | Detach the monitor → request log starts streaming. |
| Dashboard (attached) | `p` / `r` | Pause / resume the pool (built-in monitor keys). |
| Log view (detached)  | `m` / `M` | Reattach the dashboard → logging stops. |
| Log view (detached)  | `q` / `Q` | Quit the server (graceful shutdown). |
| Anywhere | `Ctrl-C` | Quit the server (graceful shutdown). |

So `q` "steps back": from the dashboard it drops you to the live log; from the log
it quits. `m` brings the dashboard back. This drives the pool's
`thread_pool_monitor_detach()` / `thread_pool_monitor_reattach()` API — reattach
reuses the existing monitor context (history is preserved).

> The interactive keys require a real terminal. With `--no-monitor`, a piped
> stdin, or `naive` mode there is no dashboard and the request log streams
> continuously; use `Ctrl-C` to quit.

Request log line format (printed only while detached):

```
[HH:MM:SS] <client-ip:port>   <METHOD> <path>[?query] -> <status> <bytes>B <ms>ms
```

Example:

```
[13:19:45] 127.0.0.1:49116    GET /render?w=80&h=80&iter=90 -> 200 19254B 0.6ms
[13:19:45] 127.0.0.1:49122    GET / -> 200 1032B 0.2ms
```

> With `--no-monitor` (or in `naive` mode) there is no dashboard to attach, so the
> request log streams continuously — handy for headless runs.

## Render parameters

`/render` accepts query params, all **hard-clamped** server-side:

| Param | Default | Range |
|-------|---------|-------|
| `w` | 640 | 16 … 2048 |
| `h` | 420 | 16 … 2048 |
| `iter` | 800 | 1 … 5000 |

Bigger `w`/`h`/`iter` = heavier render = faster pool saturation. The web UI maps
its single "Heaviness" slider onto these.

## Routes & responses

| Request | Response |
|---------|----------|
| `GET /` | `index.html` from docroot |
| `GET /style.css`, `/app.js`, … | matching static file (404 if missing/outside docroot) |
| `GET /render?...` | `image/bmp` Mandelbrot |
| Non-`GET` method | `405 Method Not Allowed` |
| Path containing `..` / outside docroot | `403 Forbidden` / `404 Not Found` |

## Performance comparison (`demo/bench.sh`)

`demo/bench.sh` is a **load-tester only — it does not start or stop any server.**
You run the server yourself in whichever mode you want; the script fires N
concurrent requests at a target URL and reports throughput + latency
percentiles. Run it once against a pool server and once against a naive server
and compare.

**Workflow:**

```bash
# terminal 1 — run a POOL server
./bin/http_server --mode pool --no-monitor --port 8080

# terminal 2 — load it (optionally sample its threads with --pid)
demo/bench.sh --url '/render?w=480&h=480&iter=1200' -c 200 \
              --pid "$(pgrep -f 'http_server --mode pool')"
```

Then stop the pool server, start a **naive** one on the same port, and run the
exact same `bench.sh` command again — compare the two outputs.

Options (all optional):

| Flag | Default | Meaning |
|------|---------|---------|
| `--url URL` | `http://127.0.0.1:8080/render?w=480&h=480&iter=1200` | full URL, or a bare `/path` joined onto `--host`/`--port` |
| `--host HOST` | `127.0.0.1` | host (used when `--url` is a bare path) |
| `--port PORT` | `8080` | port (used when `--url` is a bare path) |
| `--requests N` / `-n` | `2000` | total requests |
| `--concurrency C` / `-c` | `200` | concurrent in-flight requests |
| `--pid PID` | — | server PID to sample peak OS thread count (optional) |

Example output (one run):

```
════════════════════════════════════════════════════════════════
 Target : http://127.0.0.1:8080/render?w=480&h=480&iter=1200
 Load   : 2000 requests, concurrency 200
 Sample : threads of pid 84552
════════════════════════════════════════════════════════════════
[bench] running…

  wall time     : 1.987 s
  throughput    : 1006.5 req/s
  requests      : 2000 ok, 0 failed (of 2000)
  latency (ms)  : avg 40.71 | p50 38.01 | p90 58.05 | p99 76.96 | min 5.17 | max 87.68
  peak threads  : 17 (server pid 84552)
```

Compare the two runs: the pool holds its `peak threads` at ~workers+1 with a
tight tail, while the naive server's thread count climbs with concurrency and
its p99/max latency blows out. (Requires `curl`; otherwise only `awk`/`sort`/
`xargs` — no `ab`/`wrk`.)

## Shutdown

`Ctrl-C` (SIGINT) sets a stop flag and closes the listen socket to break `accept()`.
The main loop then detaches the monitor (restoring the terminal **before** any
drain output) and calls `thread_pool_destroy`, which waits for in-flight requests
to finish. Exit code is 0.
