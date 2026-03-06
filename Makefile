CFLAGS=-std=c11 -g -O0 -Wall -Wextra
SRCS=$(wildcard src/*.c) $(wildcard src/arch/*.c) $(wildcard src/tokenizer/*.c) $(wildcard src/parser/*.c)
OBJS=$(patsubst src/%.c,build/%.o,$(SRCS))
TARGET=build/ag_c
TEST_TARGET=build/test_tokenizer

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

build/%.o: src/%.c src/ag_c.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_TARGET): test/test_tokenizer.c build/tokenizer/tokenizer.o
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET) $(TEST_TARGET)
	$(TEST_TARGET)
	./test.sh

clean:
	rm -rf build

.PHONY: test clean
