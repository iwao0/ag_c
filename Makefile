CFLAGS=-std=c11 -g -O0 -Wall -Wextra
DEPFLAGS=-MMD -MP
UNAME_S:=$(shell uname -s)
ifeq ($(UNAME_S),Darwin)
ASAN_SANITIZERS:=address
else
ASAN_SANITIZERS:=address,leak
endif
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
TEST_PARSER=build/test_parser
TEST_E2E=build/test_e2e
TEST_CODEGEN=build/test_codegen
TEST_PREPROCESS=build/test_preprocess
TEST_FUZZ_QUICK=build/test_fuzz_quick
BENCH_TOKENIZER=build/bench_tokenizer
BENCH_PARSER=build/bench_parser
TOKENIZER_LIB_OBJS=$(OBJROOT)/tokenizer/allocator.o $(OBJROOT)/tokenizer/config_runtime.o $(OBJROOT)/tokenizer/escape.o $(OBJROOT)/tokenizer/filename_table.o $(OBJROOT)/tokenizer/literals.o $(OBJROOT)/tokenizer/scanner.o $(OBJROOT)/tokenizer/tokenizer.o $(OBJROOT)/tokenizer/token_kind.o $(OBJROOT)/tokenizer/keywords.o $(OBJROOT)/tokenizer/punctuator.o
PARSER_LIB_OBJS=$(OBJROOT)/parser/arena.o $(OBJROOT)/parser/config_runtime.o $(OBJROOT)/parser/parser.o $(OBJROOT)/parser/decl.o $(OBJROOT)/parser/diag.o $(OBJROOT)/parser/expr.o $(OBJROOT)/parser/loop_ctx.o $(OBJROOT)/parser/semantic_ctx.o $(OBJROOT)/parser/node_utils.o $(OBJROOT)/parser/stmt.o $(OBJROOT)/parser/switch_ctx.o $(OBJROOT)/parser/pragma_pack.o
DIAG_LIB_OBJS=$(patsubst src/%.c,$(OBJROOT)/%.o,$(DIAG_COMMON_SRCS) $(DIAG_MSG_SRCS))


$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(OBJROOT)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TEST_TOKENIZER): test/test_tokenizer.c test/support/tokenizer_test_hook.c $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_PARSER): test/test_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_CODEGEN): test/test_codegen.c $(OBJROOT)/arch/arm64_apple.o $(OBJROOT)/parser/arena.o $(OBJROOT)/tokenizer/escape.o $(OBJROOT)/tokenizer/filename_table.o $(OBJROOT)/tokenizer/token_kind.o $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_E2E): test/test_e2e.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_e2e.c

$(TEST_PREPROCESS): test/test_preprocess.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_preprocess.c

$(TEST_FUZZ_QUICK): test/test_fuzz_quick.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_fuzz_quick.c

$(BENCH_TOKENIZER): test/bench_tokenizer.c $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(BENCH_PARSER): test/bench_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

check-tokenizer-boundary:
	./scripts/check_tokenizer_internal_boundary.sh

check-tokenizer-perf-light:
	./scripts/check_tokenizer_perf_light.sh

log-tokenizer-hotpath-daily:
	./scripts/log_tokenizer_hotpath_daily.sh

test: $(TARGET) $(TEST_TOKENIZER) $(TEST_PARSER) $(TEST_CODEGEN) $(TEST_E2E) $(TEST_PREPROCESS) $(TEST_FUZZ_QUICK)
	$(MAKE) check-tokenizer-boundary
	$(TEST_TOKENIZER)
	$(TEST_PARSER)
	$(TEST_CODEGEN)
	$(TEST_PREPROCESS)
	$(TEST_FUZZ_QUICK)
	$(TEST_E2E)

test-asan: CFLAGS+=-fsanitize=$(ASAN_SANITIZERS) -fno-omit-frame-pointer
test-asan: clean test

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

.PHONY: test test-asan clean bench release check-tokenizer-boundary check-tokenizer-perf-light log-tokenizer-hotpath-daily

-include $(DEPS)
