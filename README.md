# Thread Pooling Project (C)

Thread-safe priority task queue and task model for building a thread pool in C (POSIX threads).

## Academic Context

This repository is Project 2 for my major coursework.
The objective is to design and implement a thread pool system + task scheduler in C as a core component for knowing concurent programing work.

## Current Status

- `task` module: implemented and tested.
- `priority_queue` module: implemented and covered by Unity tests.
- Unity test framework is integrated in `makefile`.
- Current test result:
	- `make test` -> PASS (3/3)
	- `make test-tsan` -> PASS (3/3)

## Project Structure

```text
.
├── include/
│   ├── defs.h
│   ├── priority_queue.h
│   └── task.h
├── src/
│   ├── check_core.c
│   ├── priority_queue.c
│   ├── task.c
│   └── test.c
├── makefile
└── Unity/
```

## Requirements

- Linux
- GCC
- POSIX threads (`pthread`)
- `make`

## Build And Run

### 1) Run unit tests

```bash
make test
```

### 2) Run tests with ThreadSanitizer

```bash
make test-tsan
```

### 3) Clean binaries

```bash
make clean
```

## Makefile Targets

- `make` or `make all`: build and run default tests
- `make test`: build and run Unity tests
- `make test-tsan`: build and run Unity tests with ThreadSanitizer
- `make clean`: remove generated test binaries from `bin/`

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
- `pq_pop_nonblock(...)`: non-blocking pop
- `pq_size(...)`: thread-safe queue size snapshot
- `pq_is_empty(...)`: thread-safe empty check

## Known Issues (Current)

- No failing tests at the moment.
- Potential future improvement: add more stress/concurrency test cases with 1 producer and multiple consumer threads.

## Next Improvements

- Implement thread pool core (worker lifecycle, queue integration, shutdown flow).
- Add stress tests for blocking `pq_pop` with multiple workers.
- Add CI workflow to run `make test` and `make test-tsan` automatically.

## License

No license file is currently provided in this repository.
