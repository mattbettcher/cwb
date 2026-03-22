CFLAGS=-std=c11 -g -fno-common -Wall -Wno-switch

SRC_DIR=src
OBJ_DIR=output

SRCS=$(wildcard $(SRC_DIR)/*.c)
OBJS=$(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
STAGE2_OBJS=$(SRCS:$(SRC_DIR)/%.c=stage2/$(OBJ_DIR)/%.o)

TEST_SRCS=$(wildcard test/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

# Stage 1

chibicc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/chibicc.h
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I. -c -o $@ $<

test/%.exe: chibicc test/%.c
	./chibicc -Iinclude -Itest -c -o test/$*.o test/$*.c
	$(CC) -pthread -o $@ test/$*.o -xc test/common

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./chibicc

test-all: test test-stage2

darwin-smoke: chibicc test/darwin_smoke.c
	./chibicc -target arm64-apple-darwin -o test/darwin_smoke.exe test/darwin_smoke.c
	./test/darwin_smoke.exe

darwin-call-smoke: chibicc test/darwin_call_smoke.c
	./chibicc -target arm64-apple-darwin -o test/darwin_call_smoke.exe test/darwin_call_smoke.c
	./test/darwin_call_smoke.exe

darwin-struct-smoke: chibicc test/darwin_struct_smoke.c
	./chibicc -target arm64-apple-darwin -o test/darwin_struct_smoke.exe test/darwin_struct_smoke.c
	./test/darwin_struct_smoke.exe

darwin-varargs-smoke: chibicc test/darwin_varargs_negative.c
	./chibicc -target arm64-apple-darwin -Iinclude -o test/darwin_varargs_smoke.exe test/darwin_varargs_negative.c
	./test/darwin_varargs_smoke.exe

darwin-abi-smoke: chibicc
	mkdir -p test/darwin-abi
	./chibicc -target arm64-apple-darwin -Iinclude -Itest -c -o test/darwin-abi/arith.o test/arith.c
	./chibicc -target arm64-apple-darwin -Iinclude -Itest -c -o test/darwin-abi/control.o test/control.c
	./chibicc -target arm64-apple-darwin -Iinclude -Itest -c -o test/darwin-abi/pointer.o test/pointer.c
	./chibicc -target arm64-apple-darwin -Iinclude -Itest -c -o test/darwin-abi/struct.o test/struct.c

darwin-check: darwin-smoke darwin-call-smoke darwin-struct-smoke darwin-abi-smoke darwin-varargs-smoke

# Stage 2

stage2/chibicc: $(STAGE2_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

stage2/$(OBJ_DIR)/%.o: chibicc $(SRC_DIR)/%.c $(SRC_DIR)/chibicc.h
	mkdir -p stage2/$(OBJ_DIR)
	./chibicc -I$(SRC_DIR) -I. -c -o $@ $<

stage2/test/%.exe: stage2/chibicc test/%.c
	mkdir -p stage2/test
	./stage2/chibicc -Iinclude -Itest -c -o stage2/test/$*.o test/$*.c
	$(CC) -pthread -o $@ stage2/test/$*.o -xc test/common

test-stage2: $(TESTS:test/%=stage2/test/%)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	test/driver.sh ./stage2/chibicc

# Misc.

clean:
	rm -rf chibicc $(OBJ_DIR) tmp* $(TESTS) test/*.s test/*.exe stage2
	rm -f test/*.darwin.o
	rm -rf test/darwin-abi
	rm -f test/darwin_call_smoke.exe
	rm -f test/darwin_struct_smoke.exe
	rm -f test/darwin_varargs_smoke.exe
	find * -type f '(' -name '*~' -o -name '*.o' ')' -exec rm {} ';'

.PHONY: test clean test-stage2 darwin-smoke darwin-call-smoke darwin-struct-smoke darwin-abi-smoke darwin-varargs-smoke darwin-check
