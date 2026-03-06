CFLAGS=-std=c11 -g -O0 -Wall -Wextra
SRCS=$(wildcard src/*.c) $(wildcard src/arch/*.c) $(wildcard src/tokenizer/*.c) $(wildcard src/parser/*.c)
OBJS=$(patsubst src/%.c,build/%.o,$(SRCS))
TARGET=build/ag_c
TEST_TOKENIZER=build/test_tokenizer
TEST_PARSER=build/test_parser
TEST_E2E=build/test_e2e
TEST_CODEGEN=build/test_codegen

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

build/%.o: src/%.c src/ag_c.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_TOKENIZER): test/test_tokenizer.c build/tokenizer/tokenizer.o
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_PARSER): test/test_parser.c build/parser/parser.o build/tokenizer/tokenizer.o
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_CODEGEN): test/test_codegen.c build/arch/arm64_apple.o
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_E2E): test/test_e2e.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_e2e.c

test: $(TARGET) $(TEST_TOKENIZER) $(TEST_PARSER) $(TEST_CODEGEN) $(TEST_E2E)
	$(TEST_TOKENIZER)
	$(TEST_PARSER)
	$(TEST_CODEGEN)
	$(TEST_E2E)

clean:
	rm -rf build

.PHONY: test clean
