# Demo HTTP API Reference

The demo server (`bin/http_server`) exposes **two independent listeners on separate
ports**:

| Listener | Default port | Flag | Role | Served by |
|----------|--------------|------|------|-----------|
| **Render** | `8080` | `--port` | the actual work: `/render` | thread **pool** (worker threads) |
| **Admin**  | `9090` | `--admin-port` | data-only observability | **inline** on a dedicated thread — **bypasses the pool** |

The admin listener bypasses the pool on purpose: it stays responsive even when every
worker is busy rendering (the pool orders the queue but never preempts a running task).

**Protocol notes (both listeners):**
- HTTP/1.0, one request per connection; the server sends `Connection: close` and closes
  the socket after each response — **except `GET /admin/events`**, a long-lived
  Server-Sent Events stream that stays open and is pushed to (see below).
- Only `GET` is supported. Responses carry `Content-Type` (and `Content-Length`, except the
  open-ended SSE stream).
- No TLS, auth, CORS, or rate limiting in the server — those belong to a reverse proxy in
  front. The server is intended to bind localhost/private only and never face the internet.

---

## Render listener (`:8080`)

### `GET /render`

Computes a Mandelbrot set and returns it as a 24-bit BMP image. The request is dispatched
to the thread pool; the response is the rendered image bytes.

**Query parameters** (all optional, all **hard-clamped** server-side):

| Param | Default | Min | Max | Meaning |
|-------|---------|-----|-----|---------|
| `w`   | `640`   | 16  | 2048 | image width in pixels |
| `h`   | `420`   | 16  | 2048 | image height in pixels |
| `iter`| `800`   | 1   | 5000 | max iterations per pixel (detail / heaviness) |

Out-of-range values are clamped to the nearest bound; unparseable values fall back to the
default. Bigger `w`/`h`/`iter` ⇒ heavier render ⇒ faster pool saturation.

**Responses:**

| Status | Content-Type | When |
|--------|--------------|------|
| `200 OK` | `image/bmp` | success; body is the BMP image |
| `500 Internal Server Error` | `text/plain` | render/allocation failure (`render failed`) |

**Example:**

```bash
curl -s -o out.bmp "http://127.0.0.1:8080/render?w=512&h=512&iter=1000"
```

### Other paths / methods (render listener)

| Status | When |
|--------|------|
| `404 Not Found` | any path other than `/render` |
| `405 Method Not Allowed` | any method other than `GET` |
| `400 Bad Request` | malformed request line |

> The render server no longer serves static files. A browser control panel, if used, is a
> separately deployed static frontend that calls these endpoints.

---

## Admin listener (`:9090`)

Data-only endpoints for monitoring — `GET /admin/stats`, `GET /admin/logs`, and the
real-time `GET /admin/events` (SSE) stream. Cheap, served inline (never queued on the pool),
so they answer even under full render load. The SSE broadcaster runs on its own thread and
pushes a snapshot the instant any gauge changes.

### `GET /admin/stats`

Returns a snapshot of pool and connection metrics as JSON.

**Response:** `200 OK`, `application/json`

```json
{"workers":16,"busy":3,"queue_depth":12,"active_connections":15,"total_served":2048}
```

| Field | Type | Meaning |
|-------|------|---------|
| `workers` | int | configured worker-thread count (`0` in `naive` mode / no pool) |
| `busy` | int | workers currently running a task |
| `queue_depth` | int | **approximate** count of queued, not-yet-running tasks (read without locking the queue, so it may be slightly skewed under concurrent submit/pop) |
| `active_connections` | int | in-flight connections on the render port (includes connections that produce error responses) |
| `total_served` | int | cumulative count of successful `/render` responses since start |

**Example:**

```bash
curl -s "http://127.0.0.1:9090/admin/stats"
```

### `GET /admin/logs`

Returns the most recent request-log lines from a bounded in-memory ring buffer
(last **256** lines; older lines are overwritten).

**Response:** `200 OK`, `text/plain`

```
[16:30:40] 127.0.0.1:54758    GET /render?w=400&h=400&iter=1500 -> 200 0.458MB 99.7ms
[16:30:40] 127.0.0.1:54702    GET /render?w=400&h=400&iter=1500 -> 200 0.458MB 107.4ms
```

Line format:

```
[HH:MM:SS] <client-ip:port>   <METHOD> <path>[?query] -> <status> <bytes>MB <ms>ms
```

**Example:**

```bash
curl -s "http://127.0.0.1:9090/admin/logs"
```

### `GET /admin/events`  (Server-Sent Events)

A **real-time push** stream. The browser opens it once with `EventSource`; the server holds
the connection open and writes a `data:` frame **the instant any gauge changes** (a worker
goes busy/idle, the queue grows/shrinks, a connection opens/closes, a render completes).
This replaces polling — no transition is lost in a sampling gap.

**Response:** `200 OK`, `Content-Type: text/event-stream` (stays open, no `Content-Length`).

Each frame is the same JSON as `/admin/stats`:

```
data: {"workers":4,"busy":4,"queue_depth":8,"active_connections":12,"total_served":37}

data: {"workers":4,"busy":4,"queue_depth":7,"active_connections":11,"total_served":38}
```

Behaviour:
- On (re)connect the server sends the **current snapshot immediately**, then a frame only
  when the snapshot changes.
- An idle stream gets a `: ping` comment every ~15 s as a keepalive.
- **No replay**: the stream is "from now on" (no `Last-Event-ID`); use `/admin/logs` for
  recent history. `EventSource` auto-reconnects, so the server can restart underneath it.

**Example:**

```bash
curl -sN "http://127.0.0.1:9090/admin/events"   # -N = unbuffered; Ctrl-C to stop
```

```js
const es = new EventSource("http://localhost:9099/admin/events");
es.onmessage = (e) => console.log(JSON.parse(e.data));
```

### Other paths / methods (admin listener)

| Status | When |
|--------|------|
| `404 Not Found` | any path other than `/admin/stats`, `/admin/logs`, or `/admin/events` |
| `405 Method Not Allowed` | any method other than `GET` |
| `400 Bad Request` | malformed request line |

---

## Server flags

| Flag | Default | Meaning |
|------|---------|---------|
| `--port N` | `8080` | render listener TCP port (1–65535) |
| `--admin-port N` | `9090` | admin listener TCP port (must differ from `--port`) |
| `--workers N` | number of CPU cores | worker threads in the pool (pool mode only) |
| `--mode pool\|naive` | `pool` | `pool` = dispatch renders through the thread pool; `naive` = one detached thread per connection (contrast case) |
| `--host ADDR` | `127.0.0.1` | interface both listeners bind to |
| `--no-monitor` | monitor on | disable the ncurses monitor (required for headless/scripted runs) |

**Startup banner:**

```
render backend: http://127.0.0.1:8080/render
admin data:     http://127.0.0.1:9090/admin/stats
```

## Shutdown

`Ctrl-C` (SIGINT) — or `q` in the monitor — stops both accept loops, joins the admin,
SSE-broadcaster, and key-listener threads, closes any open SSE client connections, drains
in-flight pool work, then exits `0`. The admin loop polls its socket on a 200 ms tick so
shutdown is prompt regardless of admin traffic.

---

## Running behind a reverse proxy

`demo/www/` holds a merged control-panel + monitor page. In the demo it is served on a
**separate origin from the admin endpoints**, on purpose (see "Two origins" below). The
turnkey way to run the whole thing is **`./run_demo.sh`** — a Podman pod (Caddy + nginx +
`http_server` + `www/`); the configs below are what it generates.

### Two origins (why the split)

Browsers cap **HTTP/1.1 connections at ~6 per origin**. If renders and admin polling share
one origin, a burst of heavy `/render` fetches fills all 6 connection slots and the admin
requests stall. The demo therefore serves two origins:

| Origin | Serves | Why |
|--------|--------|-----|
| **user** `:8088` | the page + `/threadpool/render` | heavy render traffic, its own 6-conn pool |
| **admin** `:9099` | `/admin/stats`, `/admin/logs`, `/admin/events` (SSE) | its **own** 6-conn pool, never starved by renders |

Because the page (`:8088`) calls the admin origin (`:9099`) **cross-origin**, the admin
proxy must add **CORS** (`Access-Control-Allow-Origin`). The page derives its admin base as
`http://<host>:9099/admin` automatically when served under `/threadpool/`.

> **Simpler alternatives.** Serve everything on **one** origin (no CORS) — but then admin can
> be starved under heavy render load. Or use **HTTP/2** (serve the edge over TLS): it
> multiplexes all requests over one connection, so the 6-per-origin cap disappears and you
> don't need the split at all.

### Turnkey: `./run_demo.sh`

```bash
./run_demo.sh            # build + start the pod, wait until healthy
WORKERS=4 ./run_demo.sh  # 4 workers < the browser's 6 conns -> the queue gauge visibly fills
./run_demo.sh down       # tear it down
```

It publishes the user origin on `:8088` and the admin origin on `:9099`, then you open
<http://localhost:8088/threadpool/>.

### nginx (user origin: static page + render)

```nginx
server {
    listen 8081;
    location /threadpool/ {
        alias /ABS/PATH/TO/demo/www/;            # trailing slash required
        try_files $uri $uri/ /threadpool/index.html;
    }
    location = /threadpool/render {
        proxy_pass http://127.0.0.1:8080/render; # query string forwarded
        # rate-limit heavy renders here (the C server has none).
    }
}
```

### Caddy (edge for both origins; admin origin adds CORS)

```caddy
# user / render origin
:8088 {
    reverse_proxy 127.0.0.1:8081               # -> nginx (static page + /render)
}

# admin origin: SEPARATE port = separate browser connection pool
:9099 {
    header Access-Control-Allow-Origin "*"     # the page calls this cross-origin
    header Access-Control-Allow-Methods "GET, OPTIONS"
    reverse_proxy 127.0.0.1:9090               # -> http_server admin (stats/logs/events)
    # basicauth / TLS would live here in production.
}
```

**SSE through a proxy:** Caddy auto-detects `text/event-stream` and flushes each frame (no
buffering), so `/admin/events` streams through unchanged. If you front the admin endpoints
with **nginx** instead, add `proxy_buffering off;` on that location or SSE frames get
buffered and never arrive in real time.

> **One proxy is enough.** Caddy alone (or nginx alone) can serve static + route render +
> handle admin/CORS. The demo uses both only because the pod was asked to include both; the
> layered "Caddy edge → nginx app" shape just mirrors a common TLS-edge topology.
