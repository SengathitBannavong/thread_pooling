# C Thread Pooling Project

A robust, thread-safe priority task queue and worker pool implementation in C using POSIX threads (pthreads).

## Overview

This project implements a multi-threaded task execution system where tasks are submitted to a centralized priority queue and executed by a pool of worker threads. It is designed to demonstrate concurrent programming principles, synchronization primitives (mutexes, condition variables), and graceful shutdown mechanisms.

## Key Features

- **Priority-Based Scheduling:** Tasks are executed based on their assigned priority (Low, Medium, High).
- **Thread-Safe Priority Queue:** Implements a blocking queue with support for both blocking and non-blocking operations.
- **Graceful Shutdown:** Ensures all pending tasks are completed before the thread pool is destroyed.
- **Unit Tested:** Comprehensive test suite using the Unity framework.
- **ThreadSanitizer Support:** Makefile includes targets for memory and race condition detection.

## Project Structure

```text
.
├── include/              # Header files
│   ├── cpu_core.h        # CPU core count utility
│   ├── priority_queue.h  # Priority queue interface
│   ├── task.h            # Task model and priority definitions
│   ├── thread_pool.h     # Thread pool core interface
│   └── threadpool.h      # Convenience wrapper header
├── src/                  # Source files
│   ├── priority_queue.c  # Queue implementation with mutex/cond
│   ├── task.c            # Task allocation and management
│   └── thread_pool.c     # Worker thread management and lifecycle
├── test/                 # Unit tests
│   ├── test_priority_queue.c
│   ├── test_task.c
│   ├── test_starvation.c
│   ├── test_deadlock.c
│   └── test_thread_pool.c
├── bin/                  # Compiled binaries (generated)
├── log/                  # Test execution logs (generated)
├── makefile              # Build system
└── Unity/                # Unity Test Framework (submodule)
```

## Requirements

- **OS:** Linux-based system
- **Compiler:** GCC (supporting C11)
- **Libraries:** POSIX threads (`pthread`)
- **Tools:** `make`

## Quick Start

### Build and Run Tests

1. **Compile all tests:**
   ```bash
   make build-tests
   ```

2. **Run the test suite:**
   ```bash
   make test
   ```

3. **Run with ThreadSanitizer (TSAN):**
   ```bash
   make test-tsan
   ```

### Usage Example

```c
#include "threadpool.h"
#include <stdio.h>
#include <unistd.h>

void sample_task(void *arg) {
    int id = *(int *)arg;
    printf("Task %d is executing...\n", id);
    sleep(1);
}

int main() {
    // Initialize pool with 4 worker threads
    thread_pool_t *pool = thread_pool_init(4);

    int task_ids[5];
    for (int i = 0; i < 5; i++) {
        task_ids[i] = i;
        // Submit tasks with Medium priority
        thread_pool_submit(pool, sample_task, &task_ids[i], TASK_PRIORITY_MEDIUM);
    }

    // Wait for tasks to finish and cleanup
    thread_pool_destroy(pool);
    return 0;
}
```

## Testing and Validation

Tests are compiled into independent binaries and execution logs are stored in the `log/` directory. The test runner provides a summary of pass/fail/timeout status for each module.

## Future Roadmap

[ ] **Aging Scheduler:** Implement a mechanism to prevent task starvation by increasing priority over time.

[ ] **Benchmarks:** Add performance evaluation metrics for throughput and latency.

[ ] **Application Layer:** Develop a sample application (e.g., an image processor or web server) utilizing the pool.

## License

No license file is currently provided in this repository.
