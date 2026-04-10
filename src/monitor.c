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


/*
 * Color pairs
 */
#define C_BUSY    1   /* green  – busy worker / RUNNING badge */
#define C_IDLE    2   /* white  – idle worker                 */
#define C_TITLE   3   /* cyan   – section headers             */
#define C_STAT    4   /* yellow – numeric values              */
#define C_HELP    5   /* blue   – key hints                   */
#define C_WARN    6   /* red    – PAUSED badge / warnings     */


/*
 * Drawing helpers
 */
static void fmt_uptime(time_t elapsed, char *buf, size_t len)
{
        snprintf(buf, len, "%02d:%02d:%02d",
                (int)(elapsed / 3600),
                (int)((elapsed % 3600) / 60),
                (int)(elapsed % 60));
}

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

/*
 * Main UI
 */
static void draw_ui(int max_y, int max_x, thread_pool_t *pool)
{
        if (!pool) return;

        /* Snapshot pool state */
        uint32_t        nw      = pool->num_workers;
        struct worker_t *ws     = pool->workers;
        uint16_t        busy    = thread_pool_num_working(pool);
        int             in_sys  = atomic_load_explicit(&pool->total_task_in_system,
                                                        memory_order_relaxed);
        bool            paused  = atomic_load_explicit(&pool->paused, memory_order_acquire);

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
                fmt_uptime(time(NULL) - pool->start_time, uptime, sizeof(uptime));

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
                snprintf(ubuf, sizeof(ubuf), "   %s  ", uptime);
                int y2, x2;
                getyx(stdscr, y2, x2);
                (void)y2;
                int target = sx + box_w - 1 - (int)strlen(ubuf);

                for (int x = x2; x < target; x++)
                        addch(' ');

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

        /* ── Key hint row ────────────────────────────────────────────────── */
        draw_rule(row, sx, box_w, NULL, false);
        row++;

        if (row < max_y - 1) {
                move(row, sx);
                addstr("║");
                if (paused) {
                        printw(" [r] resume");
                        attron(COLOR_PAIR(C_WARN) | A_DIM);
                        printw("        [q] detach");
                        attroff(COLOR_PAIR(C_WARN) | A_DIM);
                } else {
                        printw(" [p] pause");
                        attron(COLOR_PAIR(C_WARN) | A_DIM);
                        printw("        [q] detach");
                        attroff(COLOR_PAIR(C_WARN) | A_DIM);
                }
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

/*
 * Monitor thread
 *
 * First attach:  initscr() + color init
 * Reattach:      refresh()  (stdscr still valid after endwin; no re-alloc needed)
 */
static void *monitor_thread(void *arg)
{
        thread_pool_t   *pool = (thread_pool_t *)arg;
        struct monitor_t *m   = &pool->monitor;
        setlocale(LC_ALL, "");

        if(!atomic_load_explicit(&m->ever_init, memory_order_acquire)) {
                atomic_store_explicit(&m->ever_init, true, memory_order_release);
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

        }else {
                refresh();
                cbreak();
                noecho();
                curs_set(0);
                nodelay(stdscr, TRUE);
                keypad(stdscr, TRUE);
        }


        while (atomic_load_explicit(&m->running, memory_order_acquire)) {
                int max_y, max_x;
                getmaxyx(stdscr, max_y, max_x);
                draw_ui(max_y, max_x, pool);

                int ch = getch();
                switch (ch) {
                        case 'p': case 'P':
                                if (!atomic_load_explicit(&pool->paused, memory_order_acquire))
                                thread_pool_pause(pool);
                                break;
                        case 'r': case 'R':
                                if (atomic_load_explicit(&pool->paused, memory_order_acquire))
                                thread_pool_resume(pool);
                                break;
                        case 'q': case 'Q':
                                /*
                                * Detach: exit the thread loop.  The monitor context
                                * (pool->monitor) is kept alive – reattach is possible.
                                */
                                atomic_store_explicit(&m->running, false, memory_order_release);
                                break;
                        case KEY_RESIZE:
                                break;
                        default:
                                break;
                }
                napms(100);
        }
        endwin();

        return NULL;
}

void monitor_start(thread_pool_t *pool)
{
        if (!pool) return;
        struct monitor_t *m = &pool->monitor;

        if (atomic_load_explicit(&m->running, memory_order_acquire))
                return; /* already running */

        m->pool = pool;
        atomic_store_explicit(&m->running, true, memory_order_release);

        if (pthread_create(&m->tid, NULL, monitor_thread, pool) != 0) {
                atomic_store_explicit(&m->running, false, memory_order_relaxed);
                return;
        }
        m->tid_valid = true;
}

void monitor_stop(thread_pool_t *pool)
{
        if (!pool) return;
        struct monitor_t *m = &pool->monitor;

        if (atomic_load_explicit(&m->running, memory_order_acquire))
                atomic_store_explicit(&m->running, false, memory_order_release);

        if (m->tid_valid) {
                pthread_join(m->tid, NULL);
                m->tid_valid = false;
        }
}


int monitor_detach(thread_pool_t *pool)
{
        if (!pool) return -1;
        struct monitor_t *m = &pool->monitor;

        if (!atomic_load_explicit(&m->running, memory_order_acquire))
                return -1; /* already detached */

        atomic_store_explicit(&m->running, false, memory_order_release);
        pthread_join(m->tid, NULL);
        m->tid_valid = false;
        return 0;
}

int monitor_reattach(thread_pool_t *pool)
{
        if (!pool) return -1;
        struct monitor_t *m = &pool->monitor;

        if (atomic_load_explicit(&m->running, memory_order_acquire))
                return -1; /* still attached */

        if (!atomic_load_explicit(&m->ever_init, memory_order_acquire))
                return -1; /* monitor_start() was never called */

        /* Join previous thread if not yet joined */
        if (m->tid_valid) {
                pthread_join(m->tid, NULL);
                m->tid_valid = false;
        }

        atomic_store_explicit(&m->running, true, memory_order_release);

        if (pthread_create(&m->tid, NULL, monitor_thread, pool) != 0) {
                atomic_store_explicit(&m->running, false, memory_order_relaxed);
                return -1;
        }
        m->tid_valid = true;
        return 0;
}

bool monitor_attached(thread_pool_t *pool)
{
        if (!pool) return false;
        return atomic_load_explicit(&pool->monitor.running, memory_order_acquire);
}
