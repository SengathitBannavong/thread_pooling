CC = gcc
flag = -Wall -Wextra -Iinclude
target_floder = bin/
test:
	$(CC) $(flag) src/test.c src/task.c -o $(target_floder)test