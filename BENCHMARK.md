# Thread Pool Benchmark Suite

This document describes the benchmark tests available in the `benchmark/` directory and what they prove about the thread pool implementation.

## Compilation
All benchmarks can be compiled from the project root using:
```bash
make benchmarks
```
Binaries are located in the `bin/` directory.

---

## 1. CPU Bound Performance (`benchmark_cpu.c`)
**Goal:** Compare Thread Pool vs. On-Demand vs. Sequential for heavy computation.
*   **The Task:** Calculates `Fibonacci(30)` many times.
*   **What it Proves:** 
    *   **Scalability:** Shows that the Thread Pool scales linearly with the number of worker threads until it hits the hardware core limit.
    *   **Overhead:** At high task counts (e.g., 100,000), it proves that Thread Pool avoids the massive memory and scheduling overhead of creating thousands of separate `pthread` objects.

## 2. I/O Bound Performance (`benchmark_io.c`)
**Goal:** Test efficiency for tasks that wait for system resources.
*   **The Task:** Performs synchronous disk reads using `O_DIRECT`.
*   **What it Proves:** 
    *   **Latency Hiding:** Shows that having a pool allows the system to remain productive while some workers are blocked on I/O.
    *   **Throughput:** Demonstrates that for short I/O bursts, the Thread Pool is significantly faster than "On-Demand" because it avoids the thread-creation "tax" on every request.

## 3. Worker Scaling (`benchmark_scaling.c`)
**Goal:** Identify the "Sweet Spot" for concurrency on specific hardware.
*   **The Task:** Runs 10,000 tasks across a range of worker counts (1, 2, 4, 8, 16, 32, 64).
*   **What it Proves:** 
    *   **Diminishing Returns:** Shows where performance stops improving (usually around the number of physical CPU cores).
    *   **Context Switching Cost:** Proves that having *too many* workers (e.g., 64 on a 16-core machine) can sometimes slow down performance due to OS overhead.

## 4. Heterogeneous Workload (`benchmark_heterogeneous.c`)
**Goal:** Test load balancing when tasks have different weights.
*   **The Task:** Mixes 100 "Heavy" tasks (Fib 40) with 10,000 "Tiny" tasks (Fib 10).
*   **What it Proves:** 
    *   **Fairness:** Proves the pool doesn't get "clogged" by heavy tasks and continues to drain light tasks efficiently. It demonstrates the robustness of the FIFO-within-priority logic.

## 5. Burst / Idle Stability (`benchmark_stability.c`)
**Goal:** Ensure the pool remains reliable over long periods and multiple cycles.
*   **The Task:** Runs 100 cycles of 1,000-task bursts followed by idle sleep periods.
*   **What it Proves:** 
    *   **Resource Management:** Proves the implementation is free of significant memory leaks or deadlocks when transitioning between "Busy" and "Idle" states.
    *   **Reliability:** Validates that the `pthread_cond` signaling logic correctly wakes and puts workers to sleep without losing tasks.
