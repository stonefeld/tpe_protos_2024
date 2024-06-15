CFLAGS=-std=c11 -Iinclude -pedantic -pedantic-errors -g -Wall -Werror -Wextra -D_POSIX_C_SOURCE=200112L -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
LDFLAGS=-fsanitize=address

SRC=$(wildcard src/*.c)
OBJ=$(patsubst src/%.c,build/%.o,$(SRC))
BIN=build/smtpd

all: dir $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $^ $(LDFLAGS)

build/%.o: src/%.c
	$(CC) -o $@ -c $< $(CFLAGS)

dir:
	mkdir -p build

clean:
	rm -rf build
