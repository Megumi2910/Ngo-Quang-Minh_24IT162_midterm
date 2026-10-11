CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -pedantic -std=c11 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -Iinclude
SRCS = src/main.c src/file_info.c src/sort.c src/display.c src/traverse.c
OBJS = $(SRCS:.c=.o)
TARGET = my_ls

.PHONY: all clean test debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

%.o: %.c include/ls.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

test: all
	bash tests/test_runner.sh

debug: CFLAGS += -g -O0 -DDEBUG
debug: clean all
