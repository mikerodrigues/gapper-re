CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic $(shell pkg-config --cflags sdl2)
LDLIBS += $(shell pkg-config --libs sdl2) -lm

.PHONY: all clean test

all: gapper

gapper: gapper.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

test: gapper
	./gapper --self-test

clean:
	rm -f gapper
