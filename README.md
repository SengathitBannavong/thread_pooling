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
- **Libraries:** POSIX threads (`pthread`), `ncurses` (for the monitor)
- **Tools:** `make`, `git`

## Installation

### 1. Install System Dependencies

On **Fedora / RHEL / CentOS**:
```bash
sudo dnf install ncurses-devel libtsan
```

On **Ubuntu / Debian**:
```bash
sudo apt-get install libncurses5-dev libncursesw5-dev libtsan0
```

### 2. Initialize Submodules

This project uses the Unity Test Framework as a git submodule. To fetch it, run:
```bash
git submodule update --init --recursive
```

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

## Demo: HTTP Server (Docker)

The `demo/` directory contains a Mandelbrot-rendering HTTP server that uses the
thread pool to handle concurrent requests. It can be packaged and shipped as a
container image. A `Dockerfile` and `.dockerignore` are provided at the project
root. (Commands work with `docker` or `podman` — substitute as needed.)

### 1. Build the Image

```bash
docker build -t thread-pool-demo:latest .
```

The build is multi-stage: a Debian builder compiles `bin/http_server`, and the
final runtime image carries only the binary, the static assets in `demo/www/`,
and `libncursesw6`. The resulting image is ~80 MB and runs as a non-root user.

### 2. Run the Container

```bash
docker run -d --name tpd -p 8080:8080 thread-pool-demo:latest
```

Then open <http://localhost:8080/>. The server binds `0.0.0.0` inside the
container and runs headless (the ncurses dashboard is TTY-only). To pin the pool
size, append the full command with `--workers N`:

```bash
docker run -d --name tpd -p 8080:8080 thread-pool-demo:latest \
    /app/http_server --host 0.0.0.0 --port 8080 --docroot /app/www --no-monitor --workers 32
```

> **Note:** The image sets `STOPSIGNAL SIGINT` so `docker stop` triggers the
> server's graceful shutdown instead of waiting 10s for a `SIGKILL`.

### 3. Ship to a Remote Server (no registry required)

```bash
# Export, copy, and load
docker save thread-pool-demo:latest | gzip > tpd.tar.gz
scp tpd.tar.gz user@server:/tmp/
ssh user@server 'gunzip -c /tmp/tpd.tar.gz | docker load && \
    docker run -d --name tpd --restart=unless-stopped -p 8080:8080 9090:9090 thread-pool-demo:latest'
```

## Testing and Validation

Tests are compiled into independent binaries and execution logs are stored in the `log/` directory. The test runner provides a summary of pass/fail/timeout status for each module.

## Future Roadmap

[x] **Ncurses Dashboard:** Implement a real-time dashboard to monitor task queue status and worker activity.

[x] **Pause/Resume:** Add functionality to pause and resume the thread pool for maintenance or debugging.

[x] **Aging Scheduler:** Implement a mechanism to prevent task starvation by increasing priority over time.

[x] **Benchmarks:** Add performance evaluation metrics for throughput and latency.

[x] **Application Layer:** Develop a sample application (e.g., an image processor or web server) utilizing the pool.

## License

No license file is currently provided in this repository.
