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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define REQ_MAX 4096
#define SMALL_BODY 256
#define DEFAULT_PORT 8080
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_DOCROOT "demo/www"
#define DEFAULT_RENDER_W 640
#define DEFAULT_RENDER_H 420
#define DEFAULT_RENDER_ITER 800
#define MAX_RENDER_W 2048
#define MAX_RENDER_H 2048
#define MAX_RENDER_ITER 5000

typedef enum {
        MODE_POOL,
        MODE_NAIVE
} server_mode_t;

typedef struct {
        int port;
        int workers;
        server_mode_t mode;
        bool monitor;
        char host[64];
        char docroot[PATH_MAX];
        char docroot_real[PATH_MAX];
        thread_pool_t *pool;
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

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t listen_fd = -1;
static pthread_t main_tid;
static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

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

static const char *content_type(const char *path)
{
        const char *ext = strrchr(path, '.');
        if (!ext)
                return "application/octet-stream";
        if (strcmp(ext, ".html") == 0)
                return "text/html; charset=utf-8";
        if (strcmp(ext, ".css") == 0)
                return "text/css; charset=utf-8";
        if (strcmp(ext, ".js") == 0)
                return "application/javascript; charset=utf-8";
        return "application/octet-stream";
}

static int has_docroot_prefix(const char *root, const char *path)
{
        size_t n = strlen(root);
        return strncmp(root, path, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

static resp_t serve_file(int fd, server_ctx_t *ctx, const char *target)
{
        char rel[PATH_MAX];
        char joined[PATH_MAX];
        char resolved[PATH_MAX];

        if (target[0] != '/' || strstr(target, "..") || strncmp(target, "//", 2) == 0)
                return (resp_t){ 403, (long)send_status(fd, 403, "Forbidden", "forbidden\n") };

        if (strcmp(target, "/") == 0)
                snprintf(rel, sizeof(rel), "index.html");
        else
                snprintf(rel, sizeof(rel), "%s", target + 1);

        if (rel[0] == '/' || strstr(rel, ".."))
                return (resp_t){ 403, (long)send_status(fd, 403, "Forbidden", "forbidden\n") };

        int n = snprintf(joined, sizeof(joined), "%s/%s", ctx->docroot_real, rel);
        if (n < 0 || (size_t)n >= sizeof(joined))
                return (resp_t){ 414, (long)send_status(fd, 414, "URI Too Long", "uri too long\n") };

        if (!realpath(joined, resolved) || !has_docroot_prefix(ctx->docroot_real, resolved))
                return (resp_t){ 404, (long)send_status(fd, 404, "Not Found", "not found\n") };

        struct stat st;
        if (stat(resolved, &st) != 0 || !S_ISREG(st.st_mode))
                return (resp_t){ 404, (long)send_status(fd, 404, "Not Found", "not found\n") };

        FILE *fp = fopen(resolved, "rb");
        if (!fp)
                return (resp_t){ 404, (long)send_status(fd, 404, "Not Found", "not found\n") };

        char header[512];
        n = snprintf(header, sizeof(header),
                     "HTTP/1.0 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %ld\r\n"
                     "Connection: close\r\n\r\n",
                     content_type(resolved), (long)st.st_size);
        if (n > 0)
                write_all(fd, header, (size_t)n);

        char buf[8192];
        size_t rd;
        while ((rd = fread(buf, 1, sizeof(buf), fp)) > 0)
                if (write_all(fd, buf, rd) != 0)
                        break;
        fclose(fp);
        return (resp_t){ 200, (long)st.st_size };
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
        pthread_mutex_lock(&log_mtx);
        bool attached = ctx->pool && thread_pool_monitor_attached(ctx->pool);
        if (!attached) {
                char ts[16];
                time_t now = time(NULL);
                struct tm tmv;
                localtime_r(&now, &tmv);
                strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);
                if (query && *query)
                        printf("[%s] %-21s %s %s?%s -> %d %.3fMB %.1fms\n",
                               ts, peer, method, path, query, r.status, (double)r.bytes / (1024.0 * 1024.0), ms);
                else
                        printf("[%s] %-21s %s %s -> %d %.3fMB %.1fms\n",
                               ts, peer, method, path, r.status, (double)r.bytes / (1024.0 * 1024.0), ms);
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

        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        char method[16] = "?";
        char path[PATH_MAX] = "-";
        const char *query = "";
        resp_t r = { 0, 0 };

        char req[REQ_MAX];
        ssize_t nread = recv(fd, req, sizeof(req) - 1, 0);
        if (nread <= 0) {
                close(fd);
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
                if (strcmp(target, "/render") == 0)
                        r = serve_render(fd, query);
                else
                        r = serve_file(fd, ctx, target);
        }

        close(fd);

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
                "Usage: %s [--port 8080] [--workers N] [--mode pool|naive] "
                "[--docroot demo/www] [--host 127.0.0.1] [--no-monitor]\n",
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
                } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
                        const char *mode = argv[++i];
                        if (strcmp(mode, "pool") == 0)
                                ctx->mode = MODE_POOL;
                        else if (strcmp(mode, "naive") == 0)
                                ctx->mode = MODE_NAIVE;
                        else
                                return usage(argv[0]);
                } else if (strcmp(argv[i], "--docroot") == 0 && i + 1 < argc) {
                        snprintf(ctx->docroot, sizeof(ctx->docroot), "%s", argv[++i]);
                } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
                        snprintf(ctx->host, sizeof(ctx->host), "%s", argv[++i]);
                } else if (strcmp(argv[i], "--no-monitor") == 0) {
                        ctx->monitor = false;
                } else {
                        return usage(argv[0]);
                }
        }

        if (ctx->port <= 0 || ctx->port > 65535 || ctx->workers <= 0)
                return usage(argv[0]);

        if (!realpath(ctx->docroot, ctx->docroot_real)) {
                perror("realpath docroot");
                return 1;
        }

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
        ctx->workers = get_num_core();
        if (ctx->workers <= 0)
                ctx->workers = 4;
        ctx->mode = MODE_POOL;
        ctx->monitor = true;
        snprintf(ctx->host, sizeof(ctx->host), "%s", DEFAULT_HOST);
        snprintf(ctx->docroot, sizeof(ctx->docroot), "%s", DEFAULT_DOCROOT);
}

int main(int argc, char **argv)
{
        server_ctx_t ctx;
        server_defaults(&ctx);

        int parsed = parse_args(argc, argv, &ctx);
        if (parsed != 0)
                return parsed;

        printf("sever run on: http://%s:%d/\n", ctx.host, ctx.port);

        int fd = create_listener(ctx.host, ctx.port);
        if (fd < 0) {
                perror("listen");
                return 1;
        }
        listen_fd = fd;

        /* Install the SIGINT handler and record the main thread BEFORE spawning
         * the key listener, which signals this thread to quit. */
        main_tid = pthread_self();
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_sigint;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);

        pthread_t key_tid;
        bool key_valid = false;

        if (ctx.mode == MODE_POOL) {
                printf("[INFO] thread pool initialized with %d workers\n", ctx.workers);
                ctx.pool = thread_pool_init(ctx.workers);
                if (!ctx.pool) {
                        perror("thread_pool_init");
                        close(fd);
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

                if (ctx.mode == MODE_POOL) {
                        if (thread_pool_submit(ctx.pool, handle_connection, conn,
                                               PRIORITY_MEDIUM) < 0) {
                                close(client);
                                free(conn);
                        }
                } else {
                        pthread_t tid;
                        if (pthread_create(&tid, NULL, naive_thread, conn) == 0) {
                                pthread_detach(tid);
                        } else {
                                close(client);
                                free(conn);
                        }
                }
        }

        if (listen_fd >= 0)
                close((int)listen_fd);
        listen_fd = -1;

        if (key_valid)
                pthread_join(key_tid, NULL);  /* exits within ~200ms of running=0 */

        if (ctx.pool) {
                if (thread_pool_monitor_attached(ctx.pool))
                        thread_pool_monitor_detach(ctx.pool);
                thread_pool_destroy(&ctx.pool);
        }

        restore_termios();
        return 0;
}
