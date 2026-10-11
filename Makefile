CC = cc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=c11 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -D_NETBSD_SOURCE
CPPFLAGS = -Iinclude
SRCS = src/main.c src/file_info.c src/sort.c src/display.c src/traverse.c
OBJS = src/main.o src/file_info.o src/sort.o src/display.o src/traverse.o
TARGET = my_ls

.PHONY: all clean test debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJS) -lm

src/main.o: src/main.c include/ls.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/main.c -o src/main.o

src/file_info.o: src/file_info.c include/ls.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/file_info.c -o src/file_info.o

src/sort.o: src/sort.c include/ls.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/sort.c -o src/sort.o

src/display.o: src/display.c include/ls.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/display.c -o src/display.o

src/traverse.o: src/traverse.c include/ls.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -c src/traverse.c -o src/traverse.o

clean:
	rm -f $(OBJS) $(TARGET)

test: all
	sh tests/test_runner.sh

debug: CFLAGS += -g -O0 -DDEBUG
debug: clean all
