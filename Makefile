CFLAGS=-std=c11 -g -O0 -Wall -Wextra
DEPFLAGS=-MMD -MP
DIAG_LANG?=ja
OBJROOT=build/obj/$(DIAG_LANG)
DIAG_COMMON_SRCS=$(filter-out src/diag/messages_ja.c src/diag/messages_en.c src/diag/messages_all.c,$(wildcard src/diag/*.c))
ifeq ($(DIAG_LANG),all)
DIAG_MSG_SRCS=src/diag/messages_all.c
CFLAGS+=-DDIAG_LANG_ALL
else ifeq ($(DIAG_LANG),en)
DIAG_MSG_SRCS=src/diag/messages_en.c
CFLAGS+=-DDIAG_LANG_EN
else
DIAG_MSG_SRCS=src/diag/messages_ja.c
CFLAGS+=-DDIAG_LANG_JA
endif

SRCS=$(wildcard src/*.c) $(wildcard src/config/*.c) $(wildcard src/arch/*.c) $(wildcard src/tokenizer/*.c) $(wildcard src/parser/*.c) $(wildcard src/preprocess/*.c) $(DIAG_COMMON_SRCS) $(DIAG_MSG_SRCS)
OBJS=$(patsubst src/%.c,$(OBJROOT)/%.o,$(SRCS))
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
TOKENIZER_LIB_OBJS=$(OBJROOT)/tokenizer/allocator.o $(OBJROOT)/tokenizer/config_runtime.o $(OBJROOT)/tokenizer/escape.o $(OBJROOT)/tokenizer/literals.o $(OBJROOT)/tokenizer/scanner.o $(OBJROOT)/tokenizer/tokenizer.o $(OBJROOT)/tokenizer/keywords.o $(OBJROOT)/tokenizer/punctuator.o
PARSER_LIB_OBJS=$(OBJROOT)/parser/arena.o $(OBJROOT)/parser/config_runtime.o $(OBJROOT)/parser/parser.o $(OBJROOT)/parser/decl.o $(OBJROOT)/parser/diag.o $(OBJROOT)/parser/expr.o $(OBJROOT)/parser/loop_ctx.o $(OBJROOT)/parser/semantic_ctx.o $(OBJROOT)/parser/node_utils.o $(OBJROOT)/parser/stmt.o $(OBJROOT)/parser/switch_ctx.o $(OBJROOT)/parser/pragma_pack.o
DIAG_LIB_OBJS=$(patsubst src/%.c,$(OBJROOT)/%.o,$(DIAG_COMMON_SRCS) $(DIAG_MSG_SRCS))


$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJROOT)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TEST_TOKENIZER): test/test_tokenizer.c $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_TOKENIZER_C11): test/test_tokenizer_c11.c $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_PARSER): test/test_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_CODEGEN): test/test_codegen.c $(OBJROOT)/arch/arm64_apple.o $(OBJROOT)/tokenizer/escape.o $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_E2E): test/test_e2e.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_e2e.c

$(TEST_PREPROCESS): test/test_preprocess.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_preprocess.c

$(BENCH_TOKENIZER): test/bench_tokenizer.c $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(BENCH_PARSER): test/bench_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
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

release: CFLAGS=-std=c11 -Oz -DNDEBUG -flto -Wall -Wextra
release: CFLAGS+=$(if $(filter all,$(DIAG_LANG)),-DDIAG_LANG_ALL,$(if $(filter en,$(DIAG_LANG)),-DDIAG_LANG_EN,-DDIAG_LANG_JA))
release: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)
	strip $(TARGET)

clean:
	rm -rf build

.PHONY: test clean bench release

-include $(DEPS)
