# Thread-Pool Render Backend — Demo

An application-layer demo of the thread pool: a tiny HTTP/1.0 render backend
(raw POSIX sockets, zero dependencies beyond what the pool already uses).

The C server is now backend-only:

- **render listener**: `GET /render?w=&h=&iter=` computes a Mandelbrot set and
  returns it as a BMP image.
- **admin listener**: `GET /admin/stats` and `GET /admin/logs` return data only
  and are served inline on a separate pthread, bypassing the pool.

The browser control panel is a separately deployed static frontend. The files in
`demo/www/` remain in the repo for that frontend, but this C server no longer
serves them.

## Build

```bash
make demo            # produces bin/http_server
```

## Quick start

```bash
make demo
./bin/http_server
```

Defaults:

- render backend: <http://127.0.0.1:8080/render>
- admin stats: <http://127.0.0.1:9090/admin/stats>
- admin logs: <http://127.0.0.1:9090/admin/logs>

Press **Ctrl-C** to shut down: listeners close, the admin thread exits, auxiliary
threads are joined, in-flight pool work drains, and the process exits cleanly.

## Browser demo (`run_demo.sh`)

For the full interactive demo — control panel + a **real-time** monitor dashboard in a
browser — use the Podman pod, which puts a reverse proxy in front of the backend:

```bash
./run_demo.sh            # build + start the pod (Caddy + nginx + http_server + www/)
WORKERS=4 ./run_demo.sh  # 4 workers < the browser's ~6 conns -> the queue gauge fills
./run_demo.sh down       # tear it down
```

Then open <http://localhost:8088/threadpool/>, pick a **Heavy/Max** preset, and **Send
burst** — the Busy/Queue gauges update live.

How it's wired (see `demo/doc-apis.md` for the configs):

- **Two origins** so the browser's ~6-connection-per-origin limit can't let render traffic
  starve admin polling: the page + `/render` on `:8088`, the admin endpoints on `:9099`.
- **SSE push** (`GET /admin/events`): stats stream in real time, so short bursts aren't lost
  between polls. Logs are still polled.

## Usage

```text
Usage: ./bin/http_server [--port 8080] [--admin-port 9090] [--workers N] [--mode pool|naive] [--host 127.0.0.1] [--no-monitor]
```

All flags are optional; defaults shown in brackets.

| Flag | Default | Meaning |
|------|---------|---------|
| `--port N` | `8080` | Render TCP port to listen on (1-65535). |
| `--admin-port N` | `9090` | Separate admin TCP port to listen on (1-65535). |
| `--workers N` | number of CPU cores | Worker threads in the pool (pool mode only). |
| `--mode pool\|naive` | `pool` | `pool` = use the thread pool. `naive` = spawn one detached pthread per render connection. |
| `--host ADDR` | `127.0.0.1` | Interface to bind. Keep this private; put a reverse proxy in front for public access. |
| `--no-monitor` | monitor ON | Disable the ncurses monitor. Required for headless / scripted / piped runs. |

Notes:

- The ncurses monitor only attaches in `pool` mode. In `naive` mode there is no
  pool to monitor.
- While the monitor is attached the server writes no request log lines to stdout.
  Run with `--no-monitor` when you want plain logging or scripted output.
- The C server intentionally does not implement TLS, CORS, `OPTIONS`, auth, or
  rate limiting. A reverse proxy owns those concerns.

## Recipes

**Render one image:**

```bash
./bin/http_server --no-monitor --port 8137 --admin-port 9137 &
curl -s -o out.bmp "http://127.0.0.1:8137/render?w=512&h=512&iter=1000"
curl -s "http://127.0.0.1:9137/admin/stats"
```

**More workers, custom ports:**

```bash
./bin/http_server --workers 8 --port 9000 --admin-port 9001
```

**Bounded-concurrency contrast:**

Terminal 1 — pool mode, watch threads stay fixed:

```bash
./bin/http_server --mode pool --no-monitor --port 8137 --admin-port 9137 &
ps -T -p $!
```

Terminal 2 — hammer it and poll admin stats:

```bash
for i in $(seq 1 200); do
  curl -s -o /dev/null "http://127.0.0.1:8137/render?w=512&h=512&iter=1500" &
done
curl -s "http://127.0.0.1:9137/admin/stats"
wait
```

Repeat with `--mode naive` and compare the OS thread count.

## Live monitor: detach / reattach + request log

In pool mode with the monitor on, you can toggle the ncurses dashboard at runtime.
When the monitor is detached, the terminal is restored and the server streams a
one-line log per render request; when it is reattached, dashboard drawing resumes
and stdout logging is suppressed. The same formatted log lines are also kept in a
bounded in-memory ring for `GET /admin/logs`.

Keys:

| State | Key | Effect |
|-------|-----|--------|
| Dashboard (attached) | `q` / `Q` | Detach the monitor; request log starts streaming. |
| Dashboard (attached) | `p` / `r` | Pause / resume the pool. |
| Log view (detached) | `m` / `M` | Reattach the dashboard; stdout logging stops. |
| Log view (detached) | `q` / `Q` | Quit the server. |
| Anywhere | `Ctrl-C` | Quit the server. |

Request log line format:

```text
[HH:MM:SS] <client-ip:port>   <METHOD> <path>[?query] -> <status> <MB>MB <ms>ms
```

## Render parameters

`/render` accepts query params, all hard-clamped server-side:

| Param | Default | Range |
|-------|---------|-------|
| `w` | 640 | 16 ... 2048 |
| `h` | 420 | 16 ... 2048 |
| `iter` | 800 | 1 ... 5000 |

Bigger `w`/`h`/`iter` means heavier render work and faster pool saturation.

## Routes & responses

Render listener:

| Request | Response |
|---------|----------|
| `GET /render?...` | `image/bmp` Mandelbrot |
| Other `GET` path | `404 Not Found` |
| Non-`GET` method | `405 Method Not Allowed` |

Admin listener:

| Request | Response |
|---------|----------|
| `GET /admin/stats` | `application/json`: `{"workers":N,"busy":N,"queue_depth":N,"active_connections":N,"total_served":N}` |
| `GET /admin/logs` | `text/plain`: recent request log lines from the bounded ring |
| `GET /admin/events` | `text/event-stream`: real-time SSE push of the stats snapshot on every state change (used by the dashboard) |
| Other `GET` path | `404 Not Found` |
| Non-`GET` method | `405 Method Not Allowed` |

## Performance comparison (`demo/bench.sh`)

`demo/bench.sh` is a load-tester only; it does not start or stop any server. Run
the server yourself in whichever mode you want, then point the script at
`/render`.

```bash
# terminal 1
./bin/http_server --mode pool --no-monitor --port 8080 --admin-port 9090

# terminal 2
demo/bench.sh --url '/render?w=480&h=480&iter=1200' -c 200 \
              --pid "$(pgrep -f 'http_server --mode pool')"
```

Then stop the pool server, start a naive one on the same render port, and run the
same `bench.sh` command again.

## Shutdown

`Ctrl-C` sets a stop flag and closes both listen sockets to break `accept()`.
Teardown order is: close render/admin listeners, join the admin thread, join the
SSE broadcaster (closing any open event streams) and auxiliary threads, destroy the
thread pool, then destroy the log ring mutex.
