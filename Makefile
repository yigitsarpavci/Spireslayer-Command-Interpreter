# CMPE230 Assignment 1 - Spireslayer Interpreter
# Authors: 2023400048, 2023400210

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -pedantic

SRCS = main.c parser.c state.c
OBJS = $(SRCS:.c=.o)
TARGET = spireslayer

.PHONY: default grade clean

default: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

grade:
	python3 tests/test/grader.py ./spireslayer tests/test-cases

clean:
	rm -f $(OBJS) $(TARGET)
