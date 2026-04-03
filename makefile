CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Iinclude -IUnity/src -g -pthread
TARGET_F = bin/
SRC_F = src/
UNITY = Unity/src/unity.c
TEST_F = test/
LOG_F = log/

TSAN_FLAGS = -fsanitize=thread -fno-omit-frame-pointer
TEST_TIMEOUT_SEC ?= 20
NCURSES_FLAGS = -lncursesw

# Core sources (exclude monitor.c by default)
CORE_SRCS = $(filter-out $(SRC_F)monitor.c, $(wildcard $(SRC_F)*.c))
MONITOR_SRC = $(SRC_F)monitor.c

TEST_SRCS = $(filter-out $(TEST_F)test_monitor.c, $(wildcard $(TEST_F)test_*.c))
TEST_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%,$(TEST_SRCS))
TSAN_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%_tsan,$(TEST_SRCS))

.PHONY: all build-tests run-tests test build-tests-tsan run-tests-tsan test-tsan benchmarks monitor clean

all: test benchmarks

$(TARGET_F) $(LOG_F):
	mkdir -p $@

$(TARGET_F)%: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $^ -o $@

$(TARGET_F)%_tsan: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ -o $@

# Benchmark targets
BENCH_F = benchmark/
BENCH_CPU = $(TARGET_F)benchmark_cpu
BENCH_IO = $(TARGET_F)benchmark_io
BENCH_SCALE = $(TARGET_F)benchmark_scaling
BENCH_HETERO = $(TARGET_F)benchmark_heterogeneous
BENCH_STABLE = $(TARGET_F)benchmark_stability

BENCH_SRCS = $(BENCH_F)work.c

# Conda python for plotting
CONDA_ENV = bench_env
PYTHON_BENCH = conda run --no-capture-output -n $(CONDA_ENV) python3

$(BENCH_CPU): $(BENCH_F)benchmark_cpu.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) -I$(BENCH_F) $^ -o $@

$(BENCH_IO): $(BENCH_F)benchmark_io.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) -I$(BENCH_F) $^ -o $@

$(BENCH_SCALE): $(BENCH_F)benchmark_scaling.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) -I$(BENCH_F) $^ -o $@

$(BENCH_HETERO): $(BENCH_F)benchmark_heterogeneous.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) -I$(BENCH_F) $^ -o $@

$(BENCH_STABLE): $(BENCH_F)benchmark_stability.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) -I$(BENCH_F) $^ -o $@

benchmarks: $(BENCH_CPU) $(BENCH_IO) $(BENCH_SCALE) $(BENCH_HETERO) $(BENCH_STABLE)

# Monitor target
MONITOR_BIN = $(TARGET_F)test_monitor
MONITOR_TSAN_BIN = $(TARGET_F)test_monitor_tsan

$(MONITOR_BIN): $(TEST_F)test_monitor.c $(MONITOR_SRC) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) $^ -o $@ $(NCURSES_FLAGS)

$(MONITOR_TSAN_BIN): $(TEST_F)test_monitor.c $(MONITOR_SRC) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ -o $@ $(NCURSES_FLAGS)

monitor: $(MONITOR_BIN)
	./$(MONITOR_BIN)

run-benchmarks: benchmarks
	mkdir -p benchmark/res
	./$(BENCH_CPU) 100 5
	./$(BENCH_CPU) 1000 5
	./$(BENCH_CPU) 10000 5
	./$(BENCH_CPU) 100000 3
	./$(BENCH_IO) 100 5
	./$(BENCH_IO) 1000 5
	./$(BENCH_IO) 10000 5
	./$(BENCH_SCALE)
	./$(BENCH_HETERO)
	./$(BENCH_STABLE)

plot-all: run-benchmarks
	@for f in benchmark/res/*.txt; do \
		echo "Plotting $$f..."; \
		$(PYTHON_BENCH) benchmark/plot/plot.py $$f; \
	done

build-tests: $(TEST_BINS) $(MONITOR_BIN)

build-tests-tsan: $(TSAN_BINS) $(MONITOR_TSAN_BIN)

run-tests: build-tests | $(LOG_F)
	@fails=0; total=0; \
	for t in $(TEST_BINS); do \
		name=$$(basename $$t); \
		total=$$((total+1)); \
		echo "[RUN] $$name"; \
		if timeout $(TEST_TIMEOUT_SEC)s ./$$t > $(LOG_F)$$name.log 2>&1; then \
			echo "[PASS] $$name (log: $(LOG_F)$$name.log)"; \
		else \
			code=$$?; \
			if [ $$code -eq 124 ]; then \
				echo "[TIMEOUT] $$name after $(TEST_TIMEOUT_SEC)s (log: $(LOG_F)$$name.log)"; \
			else \
				echo "[FAIL] $$name (log: $(LOG_F)$$name.log)"; \
			fi; \
			fails=$$((fails+1)); \
		fi; \
	done; \
	echo "[SUMMARY] total=$$total fail=$$fails pass=$$((total-fails))"; \
	test $$fails -eq 0

run-tests-tsan: build-tests-tsan | $(LOG_F)
	@fails=0; total=0; \
	for t in $(TSAN_BINS); do \
		name=$$(basename $$t); \
		total=$$((total+1)); \
		echo "[RUN] $$name"; \
		if TSAN_OPTIONS=halt_on_error=1 timeout $(TEST_TIMEOUT_SEC)s ./$$t > $(LOG_F)$$name.log 2>&1; then \
			echo "[PASS] $$name (log: $(LOG_F)$$name.log)"; \
		else \
			code=$$?; \
			if [ $$code -eq 124 ]; then \
				echo "[TIMEOUT] $$name after $(TEST_TIMEOUT_SEC)s (log: $(LOG_F)$$name.log)"; \
			else \
				echo "[FAIL] $$name (log: $(LOG_F)$$name.log)"; \
			fi; \
			fails=$$((fails+1)); \
		fi; \
	done; \
	echo "[SUMMARY] total=$$total fail=$$fails pass=$$((total-fails))"; \
	test $$fails -eq 0

test: run-tests

test-tsan: run-tests-tsan

clean-photo:
	rm -r benchmark/plot/*.png

clean:
	rm -f $(TEST_BINS) $(TSAN_BINS) $(BENCH_CPU) $(BENCH_IO) $(BENCH_PRIO) $(BENCH_SCALE) $(BENCH_HETERO) $(BENCH_STABLE) $(MONITOR_BIN)
	rm -rf $(LOG_F)
