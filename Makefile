CC=gcc
CFLAGS=-Wall -std=c99 -pedantic -pthread

# Descomentar para debuguear
# CFLAGS+=-g
# LDFLAGS=-fsanitize=address

SRC=$(wildcard src/*.c)
OBJ=$(patsubst src/%.c,build/%.o,$(SRC))
BIN=build/smtpd

.PHONY: build

all: dir build

build: $(OBJ)
	$(CC) -o $(BIN) $^ $(LDFLAGS)

build/%.o: src/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

dir:
	mkdir -p build

clean:
	rm -rf build
