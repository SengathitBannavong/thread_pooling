/**
 * monitor.c – ncurses real-time thread pool monitor
 *
 * Displays worker activity, queue depth, utilization bar and a live
 * sparkline of task throughput.  Runs in its own pthread; communicates
 * with the pool only through atomic loads and the public API.
 *
 * Compile with: -lncursesw -lpthread
 */

#define _XOPEN_SOURCE_EXTENDED 1
#include <ncurses.h>
#include <locale.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "worker.h"
#include "monitor.h"
#include "internal/struct.h"

typedef struct thread_pool thread_pool_t;

/* ── Color pairs ───────────────────────────────────────────────────────── */
#define C_BUSY    1   /* green  – busy worker / RUNNING badge */
#define C_IDLE    2   /* white  – idle worker                 */
#define C_TITLE   3   /* cyan   – section headers             */
#define C_STAT    4   /* yellow – numeric values              */
#define C_HELP    5   /* blue   – key hints                   */
#define C_WARN    6   /* red    – PAUSED badge / warnings     */

/* ── Activity sparkline ────────────────────────────────────────────────── */
#define SPARK_LEN  24
#define SPARK_LVLS  9
static const char *const SPARK_CH[SPARK_LVLS] =
        { " ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█" };

static int spark_buf[SPARK_LEN];
static int spark_idx = 0;
static int spark_max = 1;

/* ── Monitor state ─────────────────────────────────────────────────────── */
static pthread_t      g_tid;
static atomic_bool    g_running  = false;
static atomic_bool    g_visible  = true;
static thread_pool_t *g_pool     = NULL;
static time_t         g_start;
static atomic_bool    g_paused;   /* monitor's view of pool pause state */

/* Approximate completed-task counter (written only by monitor thread) */
static int g_completed  = 0;
static int g_last_total = 0;

/* ── Drawing helpers ───────────────────────────────────────────────────── */

static void fmt_uptime(time_t elapsed, char *buf, size_t len)
{
        snprintf(buf, len, "%02d:%02d:%02d",
                (int)(elapsed / 3600),
                (int)((elapsed % 3600) / 60),
                (int)(elapsed % 60));
}

/*
 * Fill remaining columns on the current row up to the right border,
 * then write the ║ character.
 * start_x  – column of the left ║
 * box_width – total width including both ║ characters
 */
static void fill_close(int start_x, int box_width)
{
        int y, x;
        getyx(stdscr, y, x);
        (void)y;
        int right = start_x + box_width - 1;
        for (; x < right; x++) addch(' ');
        addstr("║");
}

/*
 * Draw a full horizontal rule with optional centred title:
 *   heavy=true  → ╠══ TITLE ═══╣
 *   heavy=false → ╠── TITLE ───╣   (title may be NULL for a plain rule)
 */
static void draw_rule(int row, int start_x, int box_width,
                       const char *title, bool heavy)
{
        const char *h  = heavy ? "═" : "─";
        int inner = box_width - 2;
        
        move(row, start_x);
        addstr("╠");
        
        if (title && title[0]) {
                addstr(h); addstr(h); addstr(" ");
                attron(COLOR_PAIR(C_TITLE) | A_BOLD);
                addstr(title);
                attroff(COLOR_PAIR(C_TITLE) | A_BOLD);
                addstr(" ");
                int used = 2 + 1 + (int)strlen(title) + 1; /* ══ sp title sp */
                int fill = inner - used;
                if (fill < 0) fill = 0;
                for (int i = 0; i < fill; i++) addstr(h);
        } else {
                for (int i = 0; i < inner; i++) addstr(h);
        }
        addstr("╣");
}

/*
 * Draw `width` block characters representing current/total at the
 * current cursor position.  Filled part uses color_on, empty uses color_off.
 */
static void draw_bar(int width, int current, int total,
                      int color_on, int color_off)
{
        if (width <= 0) return;
        int fill = (total > 0) ? (current * width) / total : 0;
        if (fill > width) fill = width;
        
        attron(COLOR_PAIR(color_on) | A_BOLD);
        for (int i = 0; i < fill; i++) addstr("█");
        attroff(COLOR_PAIR(color_on) | A_BOLD);
        
        attron(COLOR_PAIR(color_off) | A_DIM);
        for (int i = fill; i < width; i++) addstr("░");
        attroff(COLOR_PAIR(color_off) | A_DIM);
}

/* ── Main UI ───────────────────────────────────────────────────────────── */

static void draw_ui(int max_y, int max_x)
{
        if (!g_pool) return;
        
        /* Snapshot pool state */
        uint32_t        nw      = g_pool->num_workers;
        struct worker_t *ws     = g_pool->workers;
        uint16_t        busy    = thread_pool_num_working(g_pool);
        int             in_sys  = atomic_load_explicit(&g_pool->total_task_in_system,
                                                        memory_order_relaxed);
        bool            paused  = atomic_load_explicit(&g_paused, memory_order_acquire);
        
        /* Update completed counter */
        if (in_sys < g_last_total)
                g_completed += g_last_total - in_sys;
        g_last_total = in_sys;
        
        /* Update sparkline */
        spark_buf[spark_idx] = in_sys;
        spark_idx = (spark_idx + 1) % SPARK_LEN;
        if (in_sys > spark_max) spark_max = in_sys;
        if (spark_max == 0)     spark_max = 1;
        
        /* Layout */
        int box_w = max_x - 4;
        if (box_w < 52) box_w = 52;
        if (box_w > 72) box_w = 72;
        int sx    = (max_x - box_w) / 2;
        if (sx < 0) sx = 0;
        int inner = box_w - 2;
        
        /* Per-worker bar width:  ║  #XX  [bar]  BUSY  ║
        *  prefix "  #XX  " = 7, suffix "  BUSY" = 6  */
        int bw_worker = inner - 7 - 6;
        if (bw_worker < 4) bw_worker = 4;
        
        /* Utilization bar width: ║  Utilization  [bar]  XX%  ║
        *  "  Utilization  [" = 17, "]" = 1, "  XX%" = 5     */
        int bw_util = inner - 17 - 1 - 5;
        if (bw_util < 4) bw_util = 4;
        
        clear();
        int row = 1;
        
        /* ── Top border ──────────────────────────────────────────────────── */
        move(row, sx);
        addstr("╔");
        for (int i = 1; i < box_w - 1; i++) addstr("═");
        addstr("╗");
        row++;
        
        /* ── Title row ───────────────────────────────────────────────────── */
        {
                char uptime[16];
                fmt_uptime(time(NULL) - g_start, uptime, sizeof(uptime));
        
                move(row, sx);
                addstr("║");
        
                attron(COLOR_PAIR(C_TITLE) | A_BOLD);
                printw("  Thread Pool Monitor");
                attroff(COLOR_PAIR(C_TITLE) | A_BOLD);
        
                if (paused) {
                attron(COLOR_PAIR(C_WARN) | A_BOLD | A_BLINK);
                printw("   !  PAUSED");
                attroff(COLOR_PAIR(C_WARN) | A_BOLD | A_BLINK);
                } else {
                attron(COLOR_PAIR(C_BUSY) | A_BOLD);
                printw("   ●  RUNNING");
                attroff(COLOR_PAIR(C_BUSY) | A_BOLD);
                }
        
                /* Right-align uptime */
                char ubuf[24];
                snprintf(ubuf, sizeof(ubuf), "⏱  %s  ", uptime);
                int y2, x2;
                getyx(stdscr, y2, x2);
                (void)y2;
                int target = sx + box_w - 1 - (int)strlen(ubuf);
                for (int x = x2; x < target; x++) addch(' ');
                attron(A_DIM);
                addstr(ubuf);
                attroff(A_DIM);
        
                move(row, sx + box_w - 1);
                addstr("║");
                row++;
        }
        
        /* ── Workers section ─────────────────────────────────────────────── */
        {
                char sec[48];
                snprintf(sec, sizeof(sec), "WORKERS  %u / %u busy", busy, nw);
                draw_rule(row, sx, box_w, sec, true);
                row++;
        
                for (uint32_t i = 0; i < nw && row < max_y - 6; i++) {
                bool b = atomic_load_explicit(&ws[i].busy, memory_order_relaxed);
                move(row, sx);
                addstr("║");
        
                if (b) {
                        attron(COLOR_PAIR(C_BUSY) | A_BOLD);
                        printw("  #%-2u  ", ws[i].id);
                        draw_bar(bw_worker, 1, 1, C_BUSY, C_IDLE);
                        printw("  BUSY");
                        attroff(COLOR_PAIR(C_BUSY) | A_BOLD);
                } else {
                        attron(COLOR_PAIR(C_IDLE) | A_DIM);
                        printw("  #%-2u  ", ws[i].id);
                        draw_bar(bw_worker, 0, 1, C_BUSY, C_IDLE);
                        printw("  IDLE");
                        attroff(COLOR_PAIR(C_IDLE) | A_DIM);
                }
                fill_close(sx, box_w);
                row++;
                }
        }
        
        /* ── Stats section ───────────────────────────────────────────────── */
        draw_rule(row, sx, box_w, "STATS", true);
        row++;
        
        if (row < max_y - 4) {
                /* Utilization bar */
                int pct = nw > 0 ? (int)((busy * 100u) / nw) : 0;
                move(row, sx);
                addstr("║");
                addstr("  Utilization  [");
                draw_bar(bw_util, busy, (int)nw, C_BUSY, C_IDLE);
                addstr("]");
                attron(COLOR_PAIR(C_STAT) | A_BOLD);
                printw("  %3d%%", pct);
                attroff(COLOR_PAIR(C_STAT) | A_BOLD);
                fill_close(sx, box_w);
                row++;
        }
        
        /* Numeric stats */
        int pending = in_sys - (int)busy;
        if (pending < 0) pending = 0;
        
        struct {
                const char *label;
                int         value;
                const char *unit;
        } stats[] = {
                { "Active   ", (int)busy,   " workers busy"    },
                { "Pending  ", pending,     " tasks in queue"  },
                { "In system", in_sys,      " total"           },
                { "Completed", g_completed, " tasks done"      },
        };
        
        for (size_t s = 0; s < sizeof(stats)/sizeof(*stats) && row < max_y - 3; s++) {
                move(row, sx);
                addstr("║");
                attron(A_DIM);
                printw("  %s", stats[s].label);
                attroff(A_DIM);
                printw("  ");
                attron(COLOR_PAIR(C_STAT) | A_BOLD);
                printw("%-5d", stats[s].value);
                attroff(COLOR_PAIR(C_STAT) | A_BOLD);
                attron(A_DIM);
                addstr(stats[s].unit);
                attroff(A_DIM);
                fill_close(sx, box_w);
                row++;
        }
        
        /* Sparkline */
        if (row < max_y - 3) {
                move(row, sx);
                addstr("║");
                attron(A_DIM);
                addstr("  Activity  ");
                attroff(A_DIM);
                attron(COLOR_PAIR(C_STAT));
                for (int i = 0; i < SPARK_LEN; i++) {
                int idx = (spark_idx + i) % SPARK_LEN;
                int v   = spark_buf[idx];
                int lvl = (spark_max > 0) ? (v * (SPARK_LVLS - 1)) / spark_max : 0;
                if (lvl >= SPARK_LVLS) lvl = SPARK_LVLS - 1;
                addstr(SPARK_CH[lvl]);
                }
                attroff(COLOR_PAIR(C_STAT));
                fill_close(sx, box_w);
                row++;
        }
        
        /* ── Key hint row ────────────────────────────────────────────────── */
        draw_rule(row, sx, box_w, NULL, false);
        row++;
        
        if (row < max_y - 1) {
                move(row, sx);
                addstr("║");
                attron(COLOR_PAIR(C_HELP) | A_DIM);
                if (paused)
                printw("  [h] hide   [m] show   [r] resume   [q] quit");
                else
                printw("  [h] hide   [m] show   [p] pause    [q] quit");
                attroff(COLOR_PAIR(C_HELP) | A_DIM);
                fill_close(sx, box_w);
                row++;
        }
        
        /* ── Bottom border ───────────────────────────────────────────────── */
        if (row < max_y) {
                move(row, sx);
                addstr("╚");
                for (int i = 1; i < box_w - 1; i++) addstr("═");
                addstr("╝");
        }
        
        refresh();
        }
        
        /* ── Hidden-mode hint ──────────────────────────────────────────────────── */
        
        static void draw_hidden(void)
        {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);
        (void)max_y;
        
        move(0, 0);
        attron(COLOR_PAIR(C_HELP) | A_REVERSE);
        printw(" Thread Pool Monitor  [hidden]  press m to show ");
        attroff(COLOR_PAIR(C_HELP) | A_REVERSE);
        
        int y, x;
        getyx(stdscr, y, x);
        (void)y;
        for (; x < max_x; x++) addch(' ');
        refresh();
}

/* ── Monitor thread ────────────────────────────────────────────────────── */

static void *monitor_thread(void *arg)
{
    (void)arg;
    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    curs_set(0);
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(C_BUSY,  COLOR_GREEN,  -1);
        init_pair(C_IDLE,  COLOR_WHITE,  -1);
        init_pair(C_TITLE, COLOR_CYAN,   -1);
        init_pair(C_STAT,  COLOR_YELLOW, -1);
        init_pair(C_HELP,  COLOR_BLUE,   -1);
        init_pair(C_WARN,  COLOR_RED,    -1);
    }

    g_start = time(NULL);

    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        if (atomic_load_explicit(&g_visible, memory_order_relaxed)) {
            draw_ui(max_y, max_x);
        } else {
            clear();
            draw_hidden();
        }

        int ch = getch();
        switch (ch) {
            case 'h': case 'H':
                atomic_store_explicit(&g_visible, false, memory_order_relaxed);
                break;
            case 'm': case 'M':
                atomic_store_explicit(&g_visible, true, memory_order_relaxed);
                break;
            case 'p': case 'P':
                if (g_pool && !atomic_load_explicit(&g_paused, memory_order_acquire)) {
                    thread_pool_pause(g_pool);
                    atomic_store_explicit(&g_paused, true, memory_order_release);
                }
                break;
            case 'r': case 'R':
                if (g_pool && atomic_load_explicit(&g_paused, memory_order_acquire)) {
                    thread_pool_resume(g_pool);
                    atomic_store_explicit(&g_paused, false, memory_order_release);
                }
                break;
            case 'q': case 'Q':
                atomic_store_explicit(&g_running, false, memory_order_release);
                break;
            case KEY_RESIZE:
                /* ncurses will repaint automatically on the next iteration */
                break;
            default:
                break;
        }

        napms(100);
    }

    endwin();
    return NULL;
}

/* ── Public API ────────────────────────────────────────────────────────── */

void monitor_start(thread_pool_t *pool)
{
    if (!pool) return;
    if (atomic_load_explicit(&g_running, memory_order_relaxed)) return;

    g_pool       = pool;
    g_completed  = 0;
    g_last_total = 0;
    spark_idx    = 0;
    spark_max    = 1;
    memset(spark_buf, 0, sizeof(spark_buf));

    atomic_store_explicit(&g_paused,  false, memory_order_relaxed);
    atomic_store_explicit(&g_visible, true,  memory_order_release);

    if (pthread_create(&g_tid, NULL, monitor_thread, NULL) != 0) {
        g_pool = NULL;
        return;
    }
    /* Set running AFTER a successful create so monitor_stop() is safe */
    atomic_store_explicit(&g_running, true, memory_order_release);
}

void monitor_stop(void)
{
    if (!atomic_load_explicit(&g_running, memory_order_relaxed)) return;
    atomic_store_explicit(&g_running, false, memory_order_release);
    pthread_join(g_tid, NULL);
    g_pool = NULL;
}

void monitor_wake(void)
{
    atomic_store_explicit(&g_visible, true, memory_order_relaxed);
}
