CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Iinclude -IUnity/src -g -pthread
OPFLAGS = -std=c11 -Wall -Wextra -Iinclude -IUnity/src -O3 -march=native -ffast-math -pthread
TARGET_F = bin/
SRC_F = src/
UNITY = Unity/src/unity.c
TEST_F = test/
LOG_F = log/

TSAN_FLAGS = -fsanitize=thread -fno-omit-frame-pointer
TEST_TIMEOUT_SEC ?= 20
NCURSES_FLAGS = -lncursesw

BASELINE_F = src/baseline/
BASELINE_SRCS = $(wildcard $(BASELINE_F)*.c)
BASELINE_BIN = $(TARGET_F)baseline

BASELINE_TEST_SRC = $(TEST_F)baseline/test_baseline.c
BASELINE_POOL_SRC = $(SRC_F)baseline/baseline.c
BASELINE_TEST_BIN = $(TARGET_F)test_baseline

SMOKE_TEST_SRC = $(TEST_F)test_baseline_smoke.c
SMOKE_TEST_BIN = $(TARGET_F)test_baseline_smoke

# Core sources (all files in src/)
CORE_SRCS = $(wildcard $(SRC_F)*.c)

# Test sources
# Note: we still exclude manual tests from automatic run-tests to avoid blocking
TEST_SRCS = $(wildcard $(TEST_F)test_*.c)
# But for the standard TEST_BINS used in 'make test', we might want to exclude manual ones
# to prevent automated tests from hanging on interactive UI.
AUTO_TEST_SRCS = $(filter-out %_manual.c, $(TEST_SRCS))

TEST_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%,$(AUTO_TEST_SRCS))
TSAN_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%_tsan,$(AUTO_TEST_SRCS))

# All test bins (including manual)
ALL_TEST_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%,$(TEST_SRCS),$(BASELINE_BIN))
ALL_TSAN_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%_tsan,$(TEST_SRCS),$(BASELINE_BIN)_tsan)

MONITOR_BIN = $(TARGET_F)test_monitor_manual
VALGRIND_FLAGS = --leak-check=full --show-leak-kinds=all --suppressions=ncurses.supp
VALGRIND_TEST_FLAGS = $(VALGRIND_FLAGS) --errors-for-leak-kinds=definite,possible --error-exitcode=99

.PHONY: all build-tests build-tests-all build-all-tests-tsan run-tests test build-tests-tsan run-tests-tsan test-tsan run-tests-valgrind test-valgrind benchmarks benchmarks_base run-benchmarks run-benchmarks-base perf-report plot-all monitor valgrind-monitor clean

all: test benchmarks

$(TARGET_F) $(LOG_F):
	mkdir -p $@

# Standard pattern rule for all tests
$(TARGET_F)%: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $^ -o $@ $(NCURSES_FLAGS)

$(TARGET_F)%_tsan: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ -o $@ $(NCURSES_FLAGS)

$(BASELINE_BIN): $(BASELINE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) $^ -o $@ $(NCURSES_FLAGS)

$(BASELINE_BIN)_tsan: $(BASELINE_SRCS) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ -o $@ $(NCURSES_FLAGS)

$(BASELINE_TEST_BIN): $(BASELINE_TEST_SRC) $(BASELINE_POOL_SRC) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) -Iinclude/baseline $^ -o $@

$(BASELINE_TEST_BIN)_tsan: $(BASELINE_TEST_SRC) $(BASELINE_POOL_SRC) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -Iinclude/baseline $^ -o $@

$(SMOKE_TEST_BIN): $(SMOKE_TEST_SRC) $(BASELINE_POOL_SRC) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) -Iinclude/baseline $^ -o $@

$(SMOKE_TEST_BIN)_tsan: $(SMOKE_TEST_SRC) $(BASELINE_POOL_SRC) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -Iinclude/baseline $^ -o $@

# Benchmark targets
BENCH_F = benchmark/
BENCH_FLAG = -g -fno-omit-frame-pointer
BENCH_CPU = $(TARGET_F)benchmark_cpu
BENCH_IO = $(TARGET_F)benchmark_io
BENCH_SCALE = $(TARGET_F)benchmark_scaling
BENCH_HETERO = $(TARGET_F)benchmark_heterogeneous
BENCH_STABLE = $(TARGET_F)benchmark_stability

BENCH_SRCS = $(BENCH_F)work.c
BENCH_QUEUE = $(TARGET_F)benchmark_queue_ops
BENCH_AGING = $(TARGET_F)benchmark_aging

# Conda python for plotting
CONDA_ENV = bench_env
PYTHON_BENCH = conda run --no-capture-output -n $(CONDA_ENV) python3

$(BENCH_CPU): $(BENCH_F)benchmark_cpu.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_IO): $(BENCH_F)benchmark_io.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_SCALE): $(BENCH_F)benchmark_scaling.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_HETERO): $(BENCH_F)benchmark_heterogeneous.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_STABLE): $(BENCH_F)benchmark_stability.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_QUEUE): $(BENCH_F)benchmark_queue_ops.c $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_AGING): $(BENCH_F)benchmark_aging.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) $^ -o $@ $(NCURSES_FLAGS)

# Baseline benchmark targets
BASE_F = benchmark/base/
BENCH_BASE_CPU     = $(TARGET_F)benchmark_base_cpu
BENCH_BASE_IO      = $(TARGET_F)benchmark_base_io
BENCH_BASE_QUEUE   = $(TARGET_F)benchmark_base_queue_ops
BENCH_BASE_SCALING = $(TARGET_F)benchmark_base_scaling

$(BENCH_BASE_CPU): $(BASE_F)benchmark_base_cpu.c $(BENCH_SRCS) $(BASELINE_POOL_SRC) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) -Iinclude/baseline $^ -o $@

$(BENCH_BASE_IO): $(BASE_F)benchmark_base_io.c $(BENCH_SRCS) $(BASELINE_POOL_SRC) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) -Iinclude/baseline $^ -o $@

$(BENCH_BASE_QUEUE): $(BASE_F)benchmark_base_queue_ops.c $(BASELINE_POOL_SRC) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) -Iinclude/baseline $^ -o $@

$(BENCH_BASE_SCALING): $(BASE_F)benchmark_base_scaling.c $(BENCH_SRCS) $(BASELINE_POOL_SRC) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $(BENCH_FLAG) -Iinclude/baseline $^ -o $@

benchmarks-base: $(BENCH_BASE_CPU) $(BENCH_BASE_IO) $(BENCH_BASE_QUEUE) $(BENCH_BASE_SCALING)

benchmarks: $(BENCH_CPU) $(BENCH_IO) $(BENCH_SCALE) $(BENCH_QUEUE) $(BENCH_AGING)

perf-report: benchmarks benchmarks-base
	bash perf_run.sh

build-tests: $(TEST_BINS) $(BASELINE_TEST_BIN) $(SMOKE_TEST_BIN)

build-tests-all: $(ALL_TEST_BINS)

build-tests-tsan: $(TSAN_BINS) $(BASELINE_TEST_BIN)_tsan $(SMOKE_TEST_BIN)_tsan

build-tests-all-tsan: $(ALL_TSAN_BINS)

run-benchmarks-base: benchmarks_base
	mkdir -p benchmark/res
	./$(BENCH_BASE_CPU) 100000 3
	./$(BENCH_BASE_IO) 100000 3
	./$(BENCH_BASE_SCALING) 3
	./$(BENCH_BASE_QUEUE) 10000 3

run-benchmarks: benchmarks
	mkdir -p benchmark/res
	./$(BENCH_CPU) 100000 3
	./$(BENCH_IO) 100000 3
	./$(BENCH_SCALE) 3
	./$(BENCH_QUEUE) 10000 3
	./$(BENCH_AGING)

plot-all: run-benchmarks
	@for f in benchmark/res/*.txt; do \
		echo "Plotting $$f..."; \
		$(PYTHON_BENCH) plot.py $$f; \
	done

run-tests: build-tests | $(LOG_F)
	@fails=0; total=0; \
	for t in $(TEST_BINS) $(BASELINE_TEST_BIN) ; do \
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
	for t in $(TSAN_BINS) $(BASELINE_TEST_BIN)_tsan $(SMOKE_TEST_BIN)_tsan; do \
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

run-tests-valgrind: build-tests | $(LOG_F)
	@fails=0; total=0; \
	for t in $(TEST_BINS) $(BASELINE_TEST_BIN) ; do \
		name=$$(basename $$t); \
		total=$$((total+1)); \
		echo "[VALGRIND] $$name"; \
		if timeout $(TEST_TIMEOUT_SEC)s valgrind $(VALGRIND_TEST_FLAGS) ./$$t > $(LOG_F)$$name.valgrind.log 2>&1; then \
			echo "[PASS] $$name (log: $(LOG_F)$$name.valgrind.log)"; \
		else \
			code=$$?; \
			if [ $$code -eq 124 ]; then \
				echo "[TIMEOUT] $$name after $(TEST_TIMEOUT_SEC)s (log: $(LOG_F)$$name.valgrind.log)"; \
			else \
				echo "[FAIL] $$name (log: $(LOG_F)$$name.valgrind.log)"; \
			fi; \
			fails=$$((fails+1)); \
		fi; \
	done; \
	echo "[SUMMARY] total=$$total fail=$$fails pass=$$((total-fails))"; \
	test $$fails -eq 0

test-valgrind: run-tests-valgrind

monitor: $(MONITOR_BIN)
	./$(MONITOR_BIN)

valgrind-monitor: $(MONITOR_BIN)
	valgrind $(VALGRIND_FLAGS) ./$(MONITOR_BIN)

clean-photo:
	rm -r benchmark/plot/*.png

clean:
	rm -f $(ALL_TEST_BINS) $(ALL_TSAN_BINS) $(MONITOR_BIN) $(BASELINE_TEST_BIN) $(BASELINE_TEST_BIN)_tsan $(SMOKE_TEST_BIN) $(SMOKE_TEST_BIN)_tsan \
	      $(BENCH_CPU) $(BENCH_IO) $(BENCH_SCALE) $(BENCH_HETERO) $(BENCH_STABLE) $(BENCH_QUEUE) $(BENCH_AGING) \
	      $(BENCH_BASE_CPU) $(BENCH_BASE_IO) $(BENCH_BASE_QUEUE) $(BENCH_BASE_SCALING) \
	rm -rf $(LOG_F)
