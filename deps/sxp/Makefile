# Copyright (c) 2026 Diogo Behrens
# SPDX-License-Identifier: 0BSD

CC = cc
AR = ar
CFLAGS = -O2 -std=c99 -Wall -Wextra -Wpedantic

.PHONY: all test clean
all: libsxp.a

libsxp.a: sxp.o
	$(AR) rcs $@ $^

sxp.o: sxp.c sxp.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c sxp.c -o $@

test-sxp: test.c sxp.h libsxp.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) test.c libsxp.a $(LDLIBS) -o $@

test: test-sxp
	./test-sxp

clean:
	rm -f sxp.o libsxp.a test-sxp
