CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -Iinclude
SRC := src/bme280.c src/bme280_compensate.c

all: build/example

build/example: examples/read_once.c $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

build/test_compensate: tests/test_compensate.c $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

test: build/test_compensate
	./build/test_compensate

clean:
	rm -rf build

.PHONY: all test clean
