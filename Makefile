CFLAGS=-std=c11 -g -O0 -Wall -Wextra
DEPFLAGS=-MMD -MP
SRCS=$(wildcard src/*.c) $(wildcard src/config/*.c) $(wildcard src/arch/*.c) $(wildcard src/tokenizer/*.c) $(wildcard src/parser/*.c) $(wildcard src/preprocess/*.c)
OBJS=$(patsubst src/%.c,build/%.o,$(SRCS))
DEPS=$(OBJS:.o=.d)
TARGET=build/ag_c
TEST_TOKENIZER=build/test_tokenizer
TEST_TOKENIZER_C11=build/test_tokenizer_c11
TEST_PARSER=build/test_parser
TEST_E2E=build/test_e2e
TEST_CODEGEN=build/test_codegen
TEST_PREPROCESS=build/test_preprocess
BENCH_TOKENIZER=build/bench_tokenizer
BENCH_PARSER=build/bench_parser
TOKENIZER_LIB_OBJS=build/tokenizer/allocator.o build/tokenizer/config_adapter.o build/tokenizer/escape.o build/tokenizer/literals.o build/tokenizer/scanner.o build/tokenizer/tokenizer.o build/tokenizer/keywords.o build/tokenizer/punctuator.o
PARSER_LIB_OBJS=build/parser/parser.o build/parser/parser_decl.o build/parser/parser_semantic_ctx.o build/parser/parser_node_utils.o


$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

build/%.o: src/%.c src/ag_c.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TEST_TOKENIZER): test/test_tokenizer.c $(TOKENIZER_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_TOKENIZER_C11): test/test_tokenizer_c11.c $(TOKENIZER_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_PARSER): test/test_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_CODEGEN): test/test_codegen.c build/arch/arm64_apple.o build/tokenizer/escape.o
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_E2E): test/test_e2e.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_e2e.c

$(TEST_PREPROCESS): test/test_preprocess.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_preprocess.c

$(BENCH_TOKENIZER): test/bench_tokenizer.c $(TOKENIZER_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(BENCH_PARSER): test/bench_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

test: $(TARGET) $(TEST_TOKENIZER) $(TEST_TOKENIZER_C11) $(TEST_PARSER) $(TEST_CODEGEN) $(TEST_E2E) $(TEST_PREPROCESS)
	$(TEST_TOKENIZER)
	$(TEST_TOKENIZER_C11)
	$(TEST_PARSER)
	$(TEST_CODEGEN)
	$(TEST_PREPROCESS)
	$(TEST_E2E)

bench: $(BENCH_TOKENIZER) $(BENCH_PARSER)
	$(BENCH_TOKENIZER)
	$(BENCH_PARSER)

clean:
	rm -rf build

.PHONY: test clean bench

-include $(DEPS)
