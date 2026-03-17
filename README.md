# Thread Pooling Project (C)

Thread-safe priority task queue, task model, and worker pool in C (POSIX threads).

## Academic Context

This repository is Project 2 for my major coursework.
The objective is to design and implement a thread pool system + task scheduler in C as a core component for knowing concurent programing work.

## Current Status

- `task` module: implemented and tested.
- `priority_queue` module: implemented and tested.
- `thread_pool` module: implemented with graceful shutdown support.
- Unity test framework is integrated in `makefile`.
- Tests are split into independent test binaries under `test/`.

## Project Structure

```text
.
├── include/
│   ├── cpu_core.h
│   ├── defs.h
│   ├── priority_queue.h
│   ├── task.h
│   ├── thread_pool.h
│   └── threadpool.h
├── src/
│   ├── check_core.c
│   ├── priority_queue.c
│   ├── task.c
│   └── thread_pool.c
├── test/
│   ├── test_priority_queue.c
│   ├── test_task.c
│   └── test_thread_pool.c
├── bin/
├── log/
├── makefile
└── Unity/
```

## Requirements

- Linux
- GCC
- POSIX threads (`pthread`)
- `make`

## Build And Run

### 1) Build all tests

```bash
make build-tests
```

### 2) Run all tests (with per-test logs)

```bash
make test
```

### 3) Run all tests with ThreadSanitizer (with per-test logs)

```bash
make test-tsan
```

### 4) Set custom timeout per test binary (default 20s)

```bash
make test TEST_TIMEOUT_SEC=60
```

### 5) Clean binaries and logs

```bash
make clean
```

## Makefile Targets

- `make` or `make all`: alias to `make test`
- `make build-tests`: compile all test binaries from `test/test_*.c`
- `make run-tests`: run all normal test binaries and write logs to `log/*.log`
- `make test`: alias to `make run-tests`
- `make build-tests-tsan`: compile all TSAN test binaries
- `make run-tests-tsan`: run all TSAN test binaries and write logs to `log/*.log`
- `make test-tsan`: alias to `make run-tests-tsan`
- `make clean`: remove generated binaries and `log/`

## Module Summary

### task (`include/task.h`, `src/task.c`)

- `task_create(...)`: allocate and initialize task object
- `task_destroy(...)`: free task object
- `task_priority_str(...)`: return priority label string

### priority_queue (`include/priority_queue.h`, `src/priority_queue.c`)

- `pq_init(...)`: initialize queue, mutex, and condition variable
- `pq_destroy(...)`: release remaining tasks and synchronization objects
- `pq_push(...)`: enqueue task by priority
- `pq_pop(...)`: blocking pop (waits while queue is empty)
- `pq_pop_until_shutdown(...)`: blocking pop with shutdown-aware exit
- `pq_wake_all(...)`: broadcast wake-up for all waiting workers
- `pq_pop_nonblock(...)`: non-blocking pop
- `pq_size(...)`: thread-safe queue size snapshot
- `pq_is_empty(...)`: thread-safe empty check

### thread_pool (`include/thread_pool.h`, `src/thread_pool.c`)

- `thread_pool_init(...)`: initialize queue/resources and create worker threads
- `thread_pool_submit(...)`: submit a task to the shared priority queue
- `thread_pool_destroy(...)`: graceful shutdown (drain pending work, stop workers, cleanup)

## Test Logs

- Each test binary writes output to `log/<binary_name>.log`
- Runner prints per-binary status: `PASS`, `FAIL`, or `TIMEOUT`
- A summary line is printed at the end: total/fail/pass

## Known Issues (Current)

- No failing tests at the moment.
- Potential future improvement: add more stress/concurrency test cases with 1 producer and multiple consumer threads.

## Next Improvements

- Add another task schedule (Aging task scheduler)
- Evaluation brenchmark
- Try build appication layer on top of this

## License

No license file is currently provided in this repository.
