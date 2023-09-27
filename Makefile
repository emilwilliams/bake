#!/bin/make -f

CFLAGS := -std=gnu99 -O2 -Wall -Wextra -Wpedantic -pipe

baked:
	${LINK.c} baked.c -o baked

bake:
	./baked ./baked.c

install: baked
	./baked ./install

clean:
	-rm baked

.PHONY: bake install clean
