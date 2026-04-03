#ifndef MONITOR_H
#define MONITOR_H

#include "thread_pool.h"

/**
 * Start the ncurses real-time thread pool monitor in a background thread.
 * The monitor redraws every 100 ms. Must not be called twice without an
 * intervening monitor_stop().
 *
 * Keys:
 *   h / H  – hide the UI (terminal usable again; thread keeps running)
 *   m / M  – bring the UI back
 *   p / P  – pause the pool
 *   r / R  – resume the pool
 *   q / Q  – stop the monitor thread entirely
 *
 * @param pool  Pool to observe.  Must not be NULL.
 */
void monitor_start(thread_pool_t *pool);

/**
 * Signal the monitor thread to stop and block until it exits.
 * Restores the terminal. Safe to call even if already stopped.
 */
void monitor_stop(void);

/**
 * Show the monitor UI if it was hidden with [h].
 * Thread-safe; may be called from any thread.
 */
void monitor_wake(void);

#endif /* MONITOR_H */
