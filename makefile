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
ALL_TEST_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%,$(TEST_SRCS))
ALL_TSAN_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%_tsan,$(TEST_SRCS))

MONITOR_BIN = $(TARGET_F)test_monitor_manual
VALGRIND_FLAGS = --leak-check=full --show-leak-kinds=all --suppressions=ncurses.supp

.PHONY: all build-tests build-tests-all build-all-tests-tsan run-tests test build-tests-tsan run-tests-tsan test-tsan benchmarks run-benchmarks plot-all monitor valgrind-monitor clean

all: test benchmarks

$(TARGET_F) $(LOG_F):
	mkdir -p $@

# Standard pattern rule for all tests
$(TARGET_F)%: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $^ -o $@ $(NCURSES_FLAGS)

$(TARGET_F)%_tsan: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ -o $@ $(NCURSES_FLAGS)

# Benchmark targets
BENCH_F = benchmark/
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
	$(CC) $(OPFLAGS) -I$(BENCH_F) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_IO): $(BENCH_F)benchmark_io.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_SCALE): $(BENCH_F)benchmark_scaling.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_HETERO): $(BENCH_F)benchmark_heterogeneous.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_STABLE): $(BENCH_F)benchmark_stability.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_QUEUE): $(BENCH_F)benchmark_queue_ops.c $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $^ -o $@ $(NCURSES_FLAGS)

$(BENCH_AGING): $(BENCH_F)benchmark_aging.c $(BENCH_SRCS) $(CORE_SRCS) | $(TARGET_F)
	$(CC) $(OPFLAGS) -I$(BENCH_F) $^ -o $@ $(NCURSES_FLAGS)

benchmarks: $(BENCH_CPU) $(BENCH_IO) $(BENCH_SCALE) $(BENCH_QUEUE) $(BENCH_AGING)

build-tests: $(TEST_BINS)

build-tests-all: $(ALL_TEST_BINS)

build-tests-tsan: $(TSAN_BINS)

build-tests-all-tsan: $(ALL_TSAN_BINS)

run-benchmarks: benchmarks
	mkdir -p benchmark/res
	./$(BENCH_CPU) 100000 3
	./$(BENCH_IO) 100000 3
	./$(BENCH_SCALE)
	./$(BENCH_QUEUE) 10000 3
	./$(BENCH_AGING)

plot-all: run-benchmarks
	@for f in benchmark/res/*.txt; do \
		echo "Plotting $$f..."; \
		$(PYTHON_BENCH) plot.py $$f; \
	done

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

monitor: $(MONITOR_BIN)
	./$(MONITOR_BIN)

valgrind-monitor: $(MONITOR_BIN)
	valgrind $(VALGRIND_FLAGS) ./$(MONITOR_BIN)

clean-photo:
	rm -r benchmark/plot/*.png

clean:
	rm -f $(ALL_TEST_BINS) $(ALL_TSAN_BINS) $(MONITOR_BIN) \
	      $(BENCH_CPU) $(BENCH_IO) $(BENCH_SCALE) $(BENCH_HETERO) $(BENCH_STABLE) $(BENCH_QUEUE) $(BENCH_AGING)
	rm -rf $(LOG_F)
