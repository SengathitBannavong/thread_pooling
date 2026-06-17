#define _XOPEN_SOURCE 700

#include "mandelbrot.h"
#include "threadpool.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define REQ_MAX 4096
#define SMALL_BODY 256
#define DEFAULT_PORT 8080
#define DEFAULT_ADMIN_PORT 9090
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_RENDER_W 640
#define DEFAULT_RENDER_H 420
#define DEFAULT_RENDER_ITER 800
#define MAX_RENDER_W 2048
#define MAX_RENDER_H 2048
#define MAX_RENDER_ITER 5000
#define LOG_RING_SIZE 256
#define LOG_LINE_MAX 256

typedef enum {
        MODE_POOL,
        MODE_NAIVE
} server_mode_t;

typedef struct {
        int port;
        int admin_port;
        int workers;
        uint64_t max_tasks;   /* 0 = unbounded queue */
        server_mode_t mode;
        bool monitor;
        char host[64];
        thread_pool_t *pool;
        atomic_uint active_connections;
        atomic_uint_fast64_t total_served;
} server_ctx_t;

typedef struct {
        int fd;
        server_ctx_t *ctx;
        char peer[48];
} conn_arg_t;

typedef struct {
        int status;
        long bytes;
} resp_t;

typedef struct {
        pthread_mutex_t mutex;
        char lines[LOG_RING_SIZE][LOG_LINE_MAX];
        size_t start;
        size_t count;
} log_ring_t;

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t listen_fd = -1;
static volatile sig_atomic_t admin_fd = -1;
static pthread_t main_tid;
static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;
static log_ring_t request_log;

/*
 * SSE (Server-Sent Events) push. The dashboard opens GET /admin/events and keeps
 * it open; the broadcaster thread writes a `data: {…}` frame to every subscriber
 * the instant a gauge changes — so transitions are never lost in a polling gap.
 * The thread pool is NOT modified: every gauge change is observable from
 * demo-side points (notify_event() at accept / submit / task start / task end).
 */
#define MAX_SSE_CLIENTS 64
#define STATS_JSON_MAX 256

static struct {
        pthread_mutex_t mutex;
        int fds[MAX_SSE_CLIENTS];
        int count;
} sse_clients = { .mutex = PTHREAD_MUTEX_INITIALIZER, .count = 0 };

static pthread_mutex_t bcast_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bcast_cond = PTHREAD_COND_INITIALIZER;
static uint64_t bcast_gen = 0;   /* bumped on each state change; guarded by bcast_mtx */

static void notify_event(void);   /* defined below; used by handle_connection above it */

static struct termios orig_termios;
static bool termios_saved = false;

/* Put stdin in cbreak + noecho so the key listener reads single keys without
 * Enter. Called BEFORE the monitor's initscr() so ncurses adopts this as its
 * "shell mode" and restores it on endwin() (i.e. detached log view stays in
 * cbreak). No-op when stdin is not a TTY. */
static void enable_cbreak(void)
{
        if (!isatty(STDIN_FILENO))
                return;
        if (tcgetattr(STDIN_FILENO, &orig_termios) != 0)
                return;
        termios_saved = true;
        struct termios t = orig_termios;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void restore_termios(void)
{
        if (termios_saved)
                tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void on_sigint(int signo)
{
        (void)signo;
        running = 0;
        if (listen_fd >= 0)
                close((int)listen_fd);
        if (admin_fd >= 0)
                close((int)admin_fd);
}

static int log_ring_init(log_ring_t *ring)
{
        memset(ring, 0, sizeof(*ring));
        return pthread_mutex_init(&ring->mutex, NULL);
}

static void log_ring_append(log_ring_t *ring, const char *line)
{
        pthread_mutex_lock(&ring->mutex);
        size_t pos = (ring->start + ring->count) % LOG_RING_SIZE;

        if (ring->count == LOG_RING_SIZE) {
                pos = ring->start;
                ring->start = (ring->start + 1) % LOG_RING_SIZE;
        } else {
                ring->count++;
        }

        snprintf(ring->lines[pos], sizeof(ring->lines[pos]), "%s", line);
        pthread_mutex_unlock(&ring->mutex);
}

static size_t log_ring_copy(log_ring_t *ring, char *buf, size_t len)
{
        size_t used = 0;

        pthread_mutex_lock(&ring->mutex);
        for (size_t i = 0; i < ring->count && used < len; i++) {
                size_t pos = (ring->start + i) % LOG_RING_SIZE;
                int n = snprintf(buf + used, len - used, "%s", ring->lines[pos]);
                if (n < 0)
                        break;
                if ((size_t)n >= len - used) {
                        used = len - 1;
                        break;
                }
                used += (size_t)n;
        }
        pthread_mutex_unlock(&ring->mutex);

        buf[used] = '\0';
        return used;
}

static int write_all(int fd, const void *buf, size_t len)
{
        const char *p = buf;
        while (len > 0) {
                ssize_t n = send(fd, p, len, 0);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return -1;
                }
                if (n == 0)
                        return -1;
                p += n;
                len -= (size_t)n;
        }
        return 0;
}

static size_t send_status(int fd, int code, const char *reason, const char *body)
{
        char header[512];
        size_t body_len = strlen(body);
        int n = snprintf(header, sizeof(header),
                         "HTTP/1.0 %d %s\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n\r\n",
                         code, reason, body_len);
        if (n > 0)
                write_all(fd, header, (size_t)n);
        write_all(fd, body, body_len);
        return body_len;
}

static size_t send_typed(int fd, int code, const char *reason, const char *type,
                         const char *body)
{
        char header[512];
        size_t body_len = strlen(body);
        int n = snprintf(header, sizeof(header),
                         "HTTP/1.0 %d %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n\r\n",
                         code, reason, type, body_len);
        if (n > 0)
                write_all(fd, header, (size_t)n);
        write_all(fd, body, body_len);
        return body_len;
}

static int query_int(const char *query, const char *key, int def, int min, int max)
{
        size_t key_len = strlen(key);
        const char *p = query;

        while (p && *p) {
                if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
                        char *end = NULL;
                        long v = strtol(p + key_len + 1, &end, 10);
                        if (end == p + key_len + 1)
                                return def;
                        if (v < min)
                                return min;
                        if (v > max)
                                return max;
                        return (int)v;
                }
                p = strchr(p, '&');
                if (p)
                        p++;
        }

        return def;
}

static resp_t serve_render(int fd, const char *query)
{
        int w = query_int(query, "w", DEFAULT_RENDER_W, 16, MAX_RENDER_W);
        int h = query_int(query, "h", DEFAULT_RENDER_H, 16, MAX_RENDER_H);
        int iter = query_int(query, "iter", DEFAULT_RENDER_ITER, 1, MAX_RENDER_ITER);
        uint8_t *bmp = NULL;
        size_t len = 0;

        if (mandelbrot_bmp(w, h, iter, &bmp, &len) != 0)
                return (resp_t){ 500, (long)send_status(fd, 500, "Internal Server Error", "render failed\n") };

        char header[256];
        int n = snprintf(header, sizeof(header),
                         "HTTP/1.0 200 OK\r\n"
                         "Content-Type: image/bmp\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: close\r\n\r\n", len);
        if (n > 0)
                write_all(fd, header, (size_t)n);
        write_all(fd, bmp, len);
        free(bmp);
        return (resp_t){ 200, (long)len };
}

/*
 * Print one request line to stdout — but ONLY while the monitor is detached,
 * so the live ncurses TUI is never corrupted. The lock both serializes log
 * lines from concurrent workers and excludes the detach/reattach transition
 * (the controller takes the same lock), closing the attached-check race.
 */
static void log_request(server_ctx_t *ctx, const char *peer, const char *method,
                        const char *path, const char *query, resp_t r, double ms)
{
        char ts[16];
        char line[LOG_LINE_MAX];
        time_t now = time(NULL);
        struct tm tmv;

        localtime_r(&now, &tmv);
        strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
        if (query && *query)
                snprintf(line, sizeof(line),
                         "[%s] %-21s %s %s?%s -> %d %.3fMB %.1fms\n",
                         ts, peer, method, path, query, r.status,
                         (double)r.bytes / (1024.0 * 1024.0), ms);
        else
                snprintf(line, sizeof(line),
                         "[%s] %-21s %s %s -> %d %.3fMB %.1fms\n",
                         ts, peer, method, path, r.status,
                         (double)r.bytes / (1024.0 * 1024.0), ms);

        log_ring_append(&request_log, line);

        pthread_mutex_lock(&log_mtx);
        bool attached = ctx->pool && thread_pool_monitor_attached(ctx->pool);
        if (!attached) {
                fputs(line, stdout);
                fflush(stdout);
        }
        pthread_mutex_unlock(&log_mtx);
}

static void handle_connection(void *arg)
{
        conn_arg_t *conn = arg;
        int fd = conn->fd;
        server_ctx_t *ctx = conn->ctx;
        char peer[48];
        snprintf(peer, sizeof(peer), "%s", conn->peer);
        free(conn);

        notify_event();   /* task started on a worker (busy up, queue down) */

        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        char method[16] = "?";
        char path[PATH_MAX] = "-";
        const char *query = "";
        resp_t r = { 0, 0 };
        bool rendered = false;

        char req[REQ_MAX];
        ssize_t nread = recv(fd, req, sizeof(req) - 1, 0);
        if (nread <= 0) {
                close(fd);
                atomic_fetch_sub_explicit(&ctx->active_connections, 1, memory_order_relaxed);
                notify_event();
                return;   /* client hung up before sending anything; nothing to log */
        }
        req[nread] = '\0';

        char target[PATH_MAX];
        if (sscanf(req, "%15s %1023s", method, target) != 2) {
                r = (resp_t){ 400, (long)send_status(fd, 400, "Bad Request", "bad request\n") };
        } else if (strcmp(method, "GET") != 0) {
                snprintf(path, sizeof(path), "%s", target);
                r = (resp_t){ 405, (long)send_status(fd, 405, "Method Not Allowed", "method not allowed\n") };
        } else {
                char *q = strchr(target, '?');
                if (q)
                        *q++ = '\0';
                else
                        q = "";
                query = q;
                snprintf(path, sizeof(path), "%s", target);
                if (strcmp(target, "/render") == 0) {
                        r = serve_render(fd, query);
                        rendered = true;
                } else {
                        r = (resp_t){ 404, (long)send_status(fd, 404, "Not Found", "not found\n") };
                }
        }

        close(fd);
        atomic_fetch_sub_explicit(&ctx->active_connections, 1, memory_order_relaxed);
        if (rendered)
                atomic_fetch_add_explicit(&ctx->total_served, 1, memory_order_relaxed);
        notify_event();   /* task done (busy down, total up, active down) */

        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                    (t1.tv_nsec - t0.tv_nsec) / 1.0e6;
        log_request(ctx, peer, method, path, query, r, ms);
}

static void *naive_thread(void *arg)
{
        handle_connection(arg);
        return NULL;
}

/* Build the stats JSON snapshot — shared by GET /admin/stats and the SSE push. */
static void format_stats_json(server_ctx_t *ctx, char *buf, size_t len)
{
        uint16_t busy = thread_pool_num_working(ctx->pool);
        uint64_t queue_depth = thread_pool_queue_depth(ctx->pool);
        unsigned active = atomic_load_explicit(&ctx->active_connections, memory_order_relaxed);
        uint64_t total = atomic_load_explicit(&ctx->total_served, memory_order_relaxed);

        snprintf(buf, len,
                 "{\"workers\":%d,\"busy\":%u,\"queue_depth\":%llu,"
                 "\"active_connections\":%u,\"total_served\":%llu}",
                 ctx->pool ? ctx->workers : 0, (unsigned)busy,
                 (unsigned long long)queue_depth, active,
                 (unsigned long long)total);
}

/*
 * Wake the SSE broadcaster: a gauge just changed. Cheap, lock-guarded signal,
 * called from the accept thread and from worker threads (handle_connection).
 * Touches no thread-pool state.
 */
static void notify_event(void)
{
        pthread_mutex_lock(&bcast_mtx);
        bcast_gen++;
        pthread_cond_signal(&bcast_cond);
        pthread_mutex_unlock(&bcast_mtx);
}

/* Write a frame to every SSE subscriber; drop+close any that error (client gone). */
static void sse_send_all(const char *frame, size_t flen)
{
        pthread_mutex_lock(&sse_clients.mutex);
        int n = 0;
        for (int i = 0; i < sse_clients.count; i++) {
                int cfd = sse_clients.fds[i];
                if (write_all(cfd, frame, flen) == 0)
                        sse_clients.fds[n++] = cfd;   /* keep the live ones */
                else
                        close(cfd);                   /* drop the dead ones */
        }
        sse_clients.count = n;
        pthread_mutex_unlock(&sse_clients.mutex);
}

/* Register an SSE subscriber fd; 0 on success, -1 if the table is full. */
static int sse_register(int fd)
{
        int rc = -1;
        pthread_mutex_lock(&sse_clients.mutex);
        if (sse_clients.count < MAX_SSE_CLIENTS) {
                sse_clients.fds[sse_clients.count++] = fd;
                rc = 0;
        }
        pthread_mutex_unlock(&sse_clients.mutex);
        return rc;
}

static void sse_close_all(void)
{
        pthread_mutex_lock(&sse_clients.mutex);
        for (int i = 0; i < sse_clients.count; i++)
                close(sse_clients.fds[i]);
        sse_clients.count = 0;
        pthread_mutex_unlock(&sse_clients.mutex);
}

/*
 * Broadcaster thread: waits for a state-change signal (or a 15 s keepalive
 * timeout), then pushes the current stats snapshot to all SSE subscribers — but
 * only when it actually changed (dedup). On timeout with no change it sends an
 * SSE comment to keep idle connections (and any proxy) alive.
 */
static void *sse_broadcaster(void *arg)
{
        server_ctx_t *ctx = arg;
        char last[STATS_JSON_MAX] = "";
        uint64_t seen = 0;

        while (running) {
                pthread_mutex_lock(&bcast_mtx);
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 15;
                while (running && bcast_gen == seen &&
                       pthread_cond_timedwait(&bcast_cond, &bcast_mtx, &ts) != ETIMEDOUT)
                        ;
                bool timed_out = (bcast_gen == seen);
                seen = bcast_gen;
                pthread_mutex_unlock(&bcast_mtx);
                if (!running)
                        break;

                char payload[STATS_JSON_MAX];
                format_stats_json(ctx, payload, sizeof(payload));

                if (strcmp(payload, last) != 0) {
                        char frame[STATS_JSON_MAX + 16];
                        int fn = snprintf(frame, sizeof(frame), "data: %s\n\n", payload);
                        if (fn > 0)
                                sse_send_all(frame, (size_t)fn);
                        memcpy(last, payload, sizeof(last));
                } else if (timed_out) {
                        sse_send_all(": ping\n\n", 8);   /* keepalive comment */
                }
        }
        return NULL;
}

static void handle_admin_connection(int fd, server_ctx_t *ctx)
{
        char req[REQ_MAX];
        char method[16];
        char target[PATH_MAX];
        char path[PATH_MAX];
        ssize_t nread = recv(fd, req, sizeof(req) - 1, 0);

        if (nread <= 0) {
                close(fd);
                return;
        }
        req[nread] = '\0';

        if (sscanf(req, "%15s %1023s", method, target) != 2) {
                send_status(fd, 400, "Bad Request", "bad request\n");
                close(fd);
                return;
        }

        char *q = strchr(target, '?');
        if (q)
                *q = '\0';
        snprintf(path, sizeof(path), "%s", target);

        if (strcmp(method, "GET") != 0) {
                send_status(fd, 405, "Method Not Allowed", "method not allowed\n");
        } else if (strcmp(path, "/admin/stats") == 0) {
                char body[STATS_JSON_MAX];
                format_stats_json(ctx, body, sizeof(body));
                send_typed(fd, 200, "OK", "application/json", body);
        } else if (strcmp(path, "/admin/events") == 0) {
                /* Long-lived SSE stream. Send headers + an immediate snapshot,
                 * THEN register with the broadcaster (so it can't interleave a
                 * write before our snapshot), and return WITHOUT closing — the
                 * broadcaster now owns this fd. */
                static const char hdr[] =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/event-stream\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Connection: keep-alive\r\n"
                    "X-Accel-Buffering: no\r\n"
                    "\r\n";
                char body[STATS_JSON_MAX];
                char frame[STATS_JSON_MAX + 16];
                format_stats_json(ctx, body, sizeof(body));
                int fn = snprintf(frame, sizeof(frame), "data: %s\n\n", body);
                if (write_all(fd, hdr, sizeof(hdr) - 1) != 0 ||
                    (fn > 0 && write_all(fd, frame, (size_t)fn) != 0) ||
                    sse_register(fd) != 0) {
                        close(fd);
                        return;
                }
                return;   /* keep open: do NOT close */
        } else if (strcmp(path, "/admin/logs") == 0) {
                char body[LOG_RING_SIZE * LOG_LINE_MAX + 1];
                log_ring_copy(&request_log, body, sizeof(body));
                send_typed(fd, 200, "OK", "text/plain", body);
        } else {
                send_status(fd, 404, "Not Found", "not found\n");
        }

        close(fd);
}

static void *admin_listener(void *arg)
{
        server_ctx_t *ctx = arg;
        int fd = (int)admin_fd;

        while (running) {
                /* Poll with a timeout instead of blocking in accept(): on
                 * shutdown, on_sigint() closes admin_fd from another thread,
                 * which does NOT wake a blocked accept() on Linux. The 200 ms
                 * tick lets this loop notice running==0 on its own so
                 * pthread_join(admin_tid) can complete. Mirrors key_listener. */
                struct pollfd pfd = { .fd = fd, .events = POLLIN };
                int pr = poll(&pfd, 1, 200);
                if (pr <= 0)
                        continue;                 /* timeout/EINTR -> re-check running */
                if (!(pfd.revents & POLLIN))
                        continue;                 /* POLLERR/POLLHUP/POLLNVAL on a closed fd */

                struct sockaddr_in cli;
                socklen_t clen = sizeof(cli);
                int client = accept(fd, (struct sockaddr *)&cli, &clen);
                if (client < 0) {
                        if (errno == EINTR)
                                continue;
                        if (!running || errno == EBADF)
                                break;
                        continue;
                }

                handle_admin_connection(client, ctx);
        }

        return NULL;
}

/*
 * Keyboard listener thread.
 *
 * Active only while the monitor is DETACHED (log view) — it sleeps while the
 * ncurses dashboard owns the keyboard, so the two never read stdin at once.
 *
 *   'm' / 'M' — reattach the monitor dashboard
 *   'q' / 'Q' — quit the server (graceful shutdown)
 *
 * (Inside the dashboard, the monitor's own 'q' key detaches it; Ctrl-C quits
 * from anywhere.) The reattach holds log_mtx so it can never interleave with a
 * worker's log line.
 */
static void *key_listener(void *arg)
{
        server_ctx_t *ctx = arg;
        bool was_attached = true;

        while (running) {
                if (thread_pool_monitor_attached(ctx->pool)) {
                        was_attached = true;
                        struct timespec ts = { 0, 100 * 1000 * 1000 };  /* 100 ms */
                        nanosleep(&ts, NULL);
                        continue;
                }

                /* Just entered the detached log view — print the key hint once. */
                if (was_attached) {
                        pthread_mutex_lock(&log_mtx);
                        printf("\n[monitor] detached — streaming request log "
                               "(press 'm' to reattach, 'q' to quit)\n");
                        fflush(stdout);
                        pthread_mutex_unlock(&log_mtx);
                        was_attached = false;
                }

                struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
                if (poll(&pfd, 1, 200) <= 0 || !(pfd.revents & POLLIN))
                        continue;

                char c;
                if (read(STDIN_FILENO, &c, 1) != 1)
                        continue;

                if (c == 'm' || c == 'M') {
                        pthread_mutex_lock(&log_mtx);
                        if (!thread_pool_monitor_attached(ctx->pool)) {
                                printf("[monitor] reattaching…\n");
                                fflush(stdout);
                                thread_pool_monitor_reattach(ctx->pool);
                        }
                        pthread_mutex_unlock(&log_mtx);
                } else if (c == 'q' || c == 'Q') {
                        /* Interrupt the main thread's blocked accept() via the
                         * SIGINT handler — closing the fd from here would not
                         * wake accept() in another thread. */
                        running = 0;
                        pthread_kill(main_tid, SIGINT);
                }
        }
        return NULL;
}

static int usage(const char *prog)
{
        fprintf(stderr,
                "Usage: %s [--port 8080] [--admin-port 9090] [--workers N] "
                "[--max-tasks N] "
                "[--mode pool|naive] [--host 127.0.0.1] [--no-monitor]\n",
                prog);
        return 2;
}

static int parse_args(int argc, char **argv, server_ctx_t *ctx)
{
        for (int i = 1; i < argc; i++) {
                if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                        ctx->port = atoi(argv[++i]);
                } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
                        ctx->workers = atoi(argv[++i]);
                } else if (strcmp(argv[i], "--max-tasks") == 0 && i + 1 < argc) {
                        ctx->max_tasks = (uint64_t)strtoull(argv[++i], NULL, 10);
                } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
                        const char *mode = argv[++i];
                        if (strcmp(mode, "pool") == 0)
                                ctx->mode = MODE_POOL;
                        else if (strcmp(mode, "naive") == 0)
                                ctx->mode = MODE_NAIVE;
                        else
                                return usage(argv[0]);
                } else if (strcmp(argv[i], "--admin-port") == 0 && i + 1 < argc) {
                        ctx->admin_port = atoi(argv[++i]);
                } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
                        snprintf(ctx->host, sizeof(ctx->host), "%s", argv[++i]);
                } else if (strcmp(argv[i], "--no-monitor") == 0) {
                        ctx->monitor = false;
                } else {
                        return usage(argv[0]);
                }
        }

        if (ctx->port <= 0 || ctx->port > 65535 ||
            ctx->admin_port <= 0 || ctx->admin_port > 65535 ||
            ctx->workers <= 0 || ctx->port == ctx->admin_port)
                return usage(argv[0]);

        return 0;
}

static int create_listener(const char *host, int port)
{
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
                return -1;

        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
                close(fd);
                errno = EINVAL;
                return -1;
        }

        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
            listen(fd, SOMAXCONN) != 0) {
                close(fd);
                return -1;
        }

        return fd;
}

static void server_defaults(server_ctx_t *ctx)
{
        memset(ctx, 0, sizeof(*ctx));
        ctx->port = DEFAULT_PORT;
        ctx->admin_port = DEFAULT_ADMIN_PORT;
        ctx->workers = get_num_core();
        if (ctx->workers <= 0)
                ctx->workers = 4;
        ctx->mode = MODE_POOL;
        ctx->monitor = true;
        snprintf(ctx->host, sizeof(ctx->host), "%s", DEFAULT_HOST);
        atomic_store_explicit(&ctx->active_connections, 0, memory_order_relaxed);
        atomic_store_explicit(&ctx->total_served, 0, memory_order_relaxed);
}

int main(int argc, char **argv)
{
        server_ctx_t ctx;
        server_defaults(&ctx);

        if (log_ring_init(&request_log) != 0) {
                perror("log_ring_init");
                return 1;
        }

        int parsed = parse_args(argc, argv, &ctx);
        if (parsed != 0) {
                pthread_mutex_destroy(&request_log.mutex);
                return parsed;
        }

        printf("render backend: http://%s:%d/render\n", ctx.host, ctx.port);
        printf("admin data:     http://%s:%d/admin/stats\n", ctx.host, ctx.admin_port);
        fflush(stdout);   /* stdout is block-buffered when not a TTY (e.g. under
                           * `podman logs`); flush so the banner shows immediately */

        int fd = create_listener(ctx.host, ctx.port);
        if (fd < 0) {
                perror("listen");
                pthread_mutex_destroy(&request_log.mutex);
                return 1;
        }
        listen_fd = fd;

        int afd = create_listener(ctx.host, ctx.admin_port);
        if (afd < 0) {
                perror("admin listen");
                close(fd);
                listen_fd = -1;
                pthread_mutex_destroy(&request_log.mutex);
                return 1;
        }
        admin_fd = afd;

        /* Install the SIGINT handler and record the main thread BEFORE spawning
         * the key listener, which signals this thread to quit. */
        main_tid = pthread_self();
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_sigint;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
        signal(SIGPIPE, SIG_IGN);   /* writing to a closed SSE client must not kill us */

        pthread_t key_tid;
        bool key_valid = false;
        pthread_t admin_tid;
        bool admin_valid = false;
        pthread_t bcast_tid;
        bool bcast_valid = false;

        if (ctx.mode == MODE_POOL) {
                printf("[INFO] thread pool initialized with %d workers\n", ctx.workers);
                fflush(stdout);
                ctx.pool = thread_pool_init(ctx.workers);
                if (ctx.pool && ctx.max_tasks > 0) {
                        thread_pool_set_max_tasks(ctx.pool, ctx.max_tasks);
                        printf("[INFO] queue capped at %llu pending tasks\n",
                               (unsigned long long)ctx.max_tasks);
                        fflush(stdout);
                }
                if (!ctx.pool) {
                        perror("thread_pool_init");
                        close(fd);
                        close(afd);
                        listen_fd = -1;
                        admin_fd = -1;
                        pthread_mutex_destroy(&request_log.mutex);
                        return 1;
                }
                /* The interactive dashboard + key listener need a real terminal. */
                if (ctx.monitor && isatty(STDIN_FILENO)) {
                        signal(SIGQUIT, SIG_IGN);  /* Ctrl-\ must not kill the demo */
                        enable_cbreak();
                        thread_pool_monitor_start(ctx.pool);
                        if (pthread_create(&key_tid, NULL, key_listener, &ctx) == 0)
                                key_valid = true;
                }
        }

        if (pthread_create(&admin_tid, NULL, admin_listener, &ctx) == 0) {
                admin_valid = true;
        } else {
                perror("pthread_create admin");
                running = 0;
                close(fd);
                close(afd);
        }

        if (pthread_create(&bcast_tid, NULL, sse_broadcaster, &ctx) == 0)
                bcast_valid = true;
        else
                perror("pthread_create sse_broadcaster");

        while (running) {
                struct sockaddr_in cli;
                socklen_t clen = sizeof(cli);
                int client = accept(fd, (struct sockaddr *)&cli, &clen);
                if (client < 0) {
                        if (errno == EINTR)
                                continue;
                        if (!running || errno == EBADF)
                                break;
                        continue;
                }

                conn_arg_t *conn = malloc(sizeof(*conn));
                if (!conn) {
                        close(client);
                        continue;
                }
                conn->fd = client;
                conn->ctx = &ctx;
                char ipbuf[INET_ADDRSTRLEN];
                if (inet_ntop(AF_INET, &cli.sin_addr, ipbuf, sizeof(ipbuf)))
                        snprintf(conn->peer, sizeof(conn->peer), "%s:%u",
                                 ipbuf, (unsigned)ntohs(cli.sin_port));
                else
                        snprintf(conn->peer, sizeof(conn->peer), "?");

                atomic_fetch_add_explicit(&ctx.active_connections, 1, memory_order_relaxed);
                if (ctx.mode == MODE_POOL) {
                        if (thread_pool_submit(ctx.pool, handle_connection, conn,
                                               PRIORITY_MEDIUM) < 0) {
                                close(client);
                                free(conn);
                                atomic_fetch_sub_explicit(&ctx.active_connections, 1, memory_order_relaxed);
                        }
                } else {
                        pthread_t tid;
                        if (pthread_create(&tid, NULL, naive_thread, conn) == 0) {
                                pthread_detach(tid);
                        } else {
                                close(client);
                                free(conn);
                                atomic_fetch_sub_explicit(&ctx.active_connections, 1, memory_order_relaxed);
                        }
                }

                notify_event();   /* connection accepted / work queued */
        }

        if (listen_fd >= 0)
                close((int)listen_fd);
        listen_fd = -1;
        if (admin_fd >= 0)
                close((int)admin_fd);
        admin_fd = -1;

        if (admin_valid)
                pthread_join(admin_tid, NULL);

        if (key_valid)
                pthread_join(key_tid, NULL);  /* exits within ~200ms of running=0 */

        if (bcast_valid) {
                notify_event();               /* wake the broadcaster so it sees running==0 */
                pthread_join(bcast_tid, NULL);
        }
        sse_close_all();                      /* close any still-open SSE client fds */

        if (ctx.pool) {
                if (thread_pool_monitor_attached(ctx.pool))
                        thread_pool_monitor_detach(ctx.pool);
                thread_pool_destroy(&ctx.pool);
        }

        restore_termios();
        pthread_mutex_destroy(&request_log.mutex);
        return 0;
}
