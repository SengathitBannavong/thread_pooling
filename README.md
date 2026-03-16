# Thread Pooling Project (C)

Thread-safe priority task queue and task model for building a thread pool in C (POSIX threads).

## Current Status

- `task` module: compiles and basic test target works.
- `priority_queue` module: implemented but still work-in-progress (build warnings/errors remain).
- [Unity test framework](https://github.com/ThrowTheSwitch/Unity) is cloned in `Unity/` but not integrated into the Makefile yet.

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

### 1) Build the current test target

```bash
make test
```

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


## Next Improvements

- Integrate Unity into Makefile test targets.
- Impiment Thread Pool core

## License

No license file is currently provided in this repository.
