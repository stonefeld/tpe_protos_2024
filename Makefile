CFLAGS=-std=c11 -Iinclude -pedantic -pedantic-errors -g -Wall -Werror -D_POSIX_C_SOURCE=200112L -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
# CFLAGS+=-Wextra
LDFLAGS=-fsanitize=address -luuid

SRC=$(wildcard src/*.c)
OBJ=$(patsubst src/%.c,build/%.o,$(SRC))
BIN=build/smtpd

all: dir $(BIN)

test: dir build/request_test
	build/request_test

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $^ $(LDFLAGS)

build/request_test: test/request_test.o src/request.o src/buffer.o
	$(CC) -o $@ $^ $(LDFLAGS) -pthread -lcheck_pic -pthread -lrt -lm -lsubunit

build/%.o: src/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

build/%.o: test/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

dir:
	mkdir -p build

clean:
	rm -rf build
