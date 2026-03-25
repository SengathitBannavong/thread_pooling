CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Iinclude -IUnity/src -g -pthread
TARGET_F = bin/
SRC_F = src/
UNITY = Unity/src/unity.c
TEST_F = test/
LOG_F = log/

TSAN_FLAGS = -fsanitize=thread -fno-omit-frame-pointer
TEST_TIMEOUT_SEC ?= 20

CORE_SRCS = $(SRC_F)task.c \
	$(SRC_F)priority_queue.c \
	$(SRC_F)thread_pool.c

TEST_SRCS = $(wildcard $(TEST_F)test_*.c)
TEST_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%,$(TEST_SRCS))
TSAN_BINS = $(patsubst $(TEST_F)%.c,$(TARGET_F)%_tsan,$(TEST_SRCS))

.PHONY: all build-tests run-tests test build-tests-tsan run-tests-tsan test-tsan clean

all: test

$(TARGET_F) $(LOG_F):
	mkdir -p $@

$(TARGET_F)%: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $^ -o $@

$(TARGET_F)%_tsan: $(TEST_F)%.c $(CORE_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ -o $@

build-tests: $(TEST_BINS)

build-tests-tsan: $(TSAN_BINS)

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

clean:
	rm -f $(TEST_BINS) $(TSAN_BINS)
	rm -rf $(LOG_F)
