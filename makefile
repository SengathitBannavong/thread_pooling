CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Iinclude -IUnity/src -g -pthread
TARGET_F = bin/
SRC_F = src/
UNITY = Unity/src/unity.c

TEST_TARGET = $(TARGET_F)test
TSAN_TARGET = $(TARGET_F)test_tsan
TSAN_FLAGS = -fsanitize=thread -fno-omit-frame-pointer

TEST_SRCS = $(SRC_F)test.c \
	$(SRC_F)task.c \
	$(SRC_F)priority_queue.c

.PHONY: all test test-tsan clean

all: test

$(TARGET_F):
	mkdir -p $(TARGET_F)

$(TEST_TARGET): $(TEST_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $^ -o $@

# run tests
test: $(TEST_TARGET)
	./$(TEST_TARGET)

# run tests + ThreadSanitizer
$(TSAN_TARGET): $(TEST_SRCS) $(UNITY) | $(TARGET_F)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ -o $@

test-tsan: $(TSAN_TARGET)
	TSAN_OPTIONS=halt_on_error=1 ./$(TSAN_TARGET)

clean:
	rm -f $(TEST_TARGET) $(TSAN_TARGET)
