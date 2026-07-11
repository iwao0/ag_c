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

SRCS=$(wildcard src/*.c) $(wildcard src/config/*.c) $(wildcard src/arch/*.c) $(wildcard src/tokenizer/*.c) $(wildcard src/parser/*.c) $(wildcard src/semantic/*.c) $(wildcard src/preprocess/*.c) $(wildcard src/ir/*.c) $(wildcard src/lowering/*.c) $(DIAG_COMMON_SRCS) $(DIAG_MSG_SRCS)
OBJS=$(patsubst src/%.c,$(OBJROOT)/%.o,$(SRCS))
DEPS=$(OBJS:.o=.d)
TARGET=build/ag_c
WASM_TARGET=build/ag_c_wasm
WASM_LINKER=build/ag_wasm_link
WASM_RUNTIME=build/libagc_runtime.o
WASM_SELFHOST_API=build/wasm_selfhost_api/ag_c_wasm_api.wasm
WASM_LINKER_SELFHOST=build/wasm_linker_selfhost/ag_wasm_link.wasm
TEST_TOKENIZER=build/test_tokenizer
TEST_PARSER=build/test_parser
TEST_E2E=build/test_e2e
TEST_PREPROCESS=build/test_preprocess
TEST_FUZZ_QUICK=build/test_fuzz_quick
TEST_IR=build/test_ir
TEST_FRAME_LAYOUT=build/test_frame_layout
TEST_IR_E2E=build/test_ir_e2e
TEST_WASM32_BACKEND=build/test_wasm32_backend
TEST_WASM32_E2E=build/test_wasm32_e2e
TEST_WASM32_OBJECT=build/test_wasm32_object
BENCH_TOKENIZER=build/bench_tokenizer
BENCH_PARSER=build/bench_parser
TOKENIZER_LIB_OBJS=$(OBJROOT)/tokenizer/allocator.o $(OBJROOT)/tokenizer/config_runtime.o $(OBJROOT)/tokenizer/cursor.o $(OBJROOT)/tokenizer/escape.o $(OBJROOT)/tokenizer/filename_table.o $(OBJROOT)/tokenizer/literals.o $(OBJROOT)/tokenizer/number.o $(OBJROOT)/tokenizer/scanner.o $(OBJROOT)/tokenizer/tokenizer.o $(OBJROOT)/tokenizer/token_kind.o $(OBJROOT)/tokenizer/keywords.o $(OBJROOT)/tokenizer/punctuator.o $(OBJROOT)/tokenizer/trigraph.o
PARSER_LIB_OBJS=$(OBJROOT)/parser/alignas_value.o $(OBJROOT)/parser/anon_tag.o $(OBJROOT)/parser/arena.o $(OBJROOT)/parser/array_suffixes.o $(OBJROOT)/parser/config_runtime.o $(OBJROOT)/parser/parser.o $(OBJROOT)/parser/decl.o $(OBJROOT)/parser/diag.o $(OBJROOT)/parser/enum_const.o $(OBJROOT)/parser/expr.o $(OBJROOT)/parser/global_registry.o $(OBJROOT)/parser/initializer_syntax.o $(OBJROOT)/parser/local_registry.o $(OBJROOT)/parser/semantic_ctx.o $(OBJROOT)/parser/semantic_pass.o $(OBJROOT)/parser/node_utils.o $(OBJROOT)/parser/stmt.o $(OBJROOT)/parser/struct_layout.o $(OBJROOT)/parser/pragma_pack.o $(OBJROOT)/parser/type.o $(OBJROOT)/parser/type_name.o $(OBJROOT)/semantic/declaration_resolution.o $(OBJROOT)/semantic/constant_expression.o $(OBJROOT)/semantic/initializer_resolution.o $(OBJROOT)/semantic/local_declaration_plan.o $(OBJROOT)/lowering/frame_layout.o $(OBJROOT)/lowering/local_storage.o $(OBJROOT)/lowering/static_data_initializer.o $(OBJROOT)/lowering/static_local_lowering.o $(OBJROOT)/lowering/vla_lowering.o $(OBJROOT)/lowering/expr_lowering.o $(OBJROOT)/lowering/cast_lowering.o $(OBJROOT)/lowering/assignment_lowering.o $(OBJROOT)/lowering/initializer_lowering.o
DIAG_LIB_OBJS=$(patsubst src/%.c,$(OBJROOT)/%.o,$(DIAG_COMMON_SRCS) $(DIAG_MSG_SRCS))
# IR (Phase 1): まだ ag_c 本体には組み込まず、単体テスト用にだけビルドする。
IR_LIB_OBJS=$(OBJROOT)/ir/ir_alloc.o $(OBJROOT)/ir/ir_print.o
WASM_MAIN_OBJ=$(OBJROOT)/main_wasm32.o
WASM_OBJS=$(filter-out $(OBJROOT)/main.o,$(OBJS)) $(WASM_MAIN_OBJ)


$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(WASM_TARGET): $(WASM_OBJS)
	$(CC) $(CFLAGS) -o $@ $(WASM_OBJS)

$(WASM_LINKER): tools/wasm_obj_linker/ag_wasm_link.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $<

$(WASM_RUNTIME): tools/wasm_obj_linker/runtime/libagc_runtime.c tools/wasm_obj_linker/runtime/parts/*.c $(WASM_TARGET)
	@mkdir -p $(dir $@)
	./$(WASM_TARGET) -c -o $@ $<

$(OBJROOT)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

$(WASM_MAIN_OBJ): src/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -DAGC_TARGET_WASM32 -c -o $@ $<

$(TEST_TOKENIZER): test/test_tokenizer.c test/support/tokenizer_test_hook.c $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_PARSER): test/test_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
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

$(TEST_IR): test/test_ir.c $(IR_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_FRAME_LAYOUT): test/test_frame_layout.c $(OBJROOT)/lowering/frame_layout.o $(OBJROOT)/lowering/local_storage.o
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_IR_E2E): test/test_ir_e2e.c $(TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_ir_e2e.c

$(TEST_WASM32_BACKEND): test/test_wasm32_backend.c $(WASM_TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_wasm32_backend.c

$(TEST_WASM32_E2E): test/test_wasm32_e2e.c $(WASM_TARGET)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_wasm32_e2e.c

$(TEST_WASM32_OBJECT): test/test_wasm32_object.c $(WASM_TARGET) $(WASM_LINKER) $(WASM_RUNTIME)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/test_wasm32_object.c

$(BENCH_TOKENIZER): test/bench_tokenizer.c $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

$(BENCH_PARSER): test/bench_parser.c $(PARSER_LIB_OBJS) $(TOKENIZER_LIB_OBJS) $(DIAG_LIB_OBJS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

# 「cc は拒否するが ag_c が受け入れてしまう」C ソースを test/fixtures/should_reject/
# 配下に集めている。レポート専用 (CI を red にしない)。
check-should-reject: $(TARGET)
	./scripts/check_should_reject.sh

wasm32-object-fixture-scan: $(WASM_TARGET)
	@bash scripts/run_wasm32_object_fixture_scan.sh --list-fail

wasm32-object-link-fixture-scan: $(WASM_TARGET) $(WASM_LINKER) $(WASM_RUNTIME)
	@bash scripts/run_wasm32_object_link_fixture_scan.sh --list-fail

wasm32-object-link-all-fixture-scan: $(WASM_TARGET) $(WASM_LINKER) $(WASM_RUNTIME)
	@bash scripts/run_wasm32_object_link_fixture_scan.sh --all-fixtures --list-fail

wasm32-wat-fixture-scan: $(WASM_TARGET)
	@bash scripts/run_wasm32_wat_fixture_scan.sh --list-fail

wasm32-object-c-testsuite-scan: $(WASM_TARGET)
	@bash scripts/run_wasm32_object_c_testsuite_scan.sh --list-fail

wasm32-object-link-c-testsuite-scan: $(WASM_TARGET) $(WASM_LINKER) $(WASM_RUNTIME)
	@bash scripts/run_wasm32_object_link_c_testsuite_scan.sh --list-fail

wasm32-wat-c-testsuite-scan: $(WASM_TARGET)
	@bash scripts/run_wasm32_wat_c_testsuite_scan.sh --list-fail

wasm32-scans: wasm32-object-fixture-scan wasm32-object-link-fixture-scan wasm32-object-link-all-fixture-scan wasm32-wat-fixture-scan wasm32-object-c-testsuite-scan wasm32-object-link-c-testsuite-scan wasm32-wat-c-testsuite-scan

test-wasm-obj-linker: $(WASM_TARGET) $(WASM_LINKER) $(WASM_RUNTIME)
	@bash tools/wasm_obj_linker/test_smoke.sh

$(WASM_SELFHOST_API): FORCE $(WASM_TARGET) $(WASM_LINKER) $(WASM_RUNTIME)
	@bash scripts/build_wasm_selfhost_api.sh build/wasm_selfhost_api

wasm-selfhost-api: $(WASM_SELFHOST_API)

test-wasm-js-api: $(WASM_SELFHOST_API)
	@node tools/wasm_js_api/test_smoke.mjs $(WASM_SELFHOST_API)
	@node tools/wasm_js_api/test_package_exports.mjs

$(WASM_LINKER_SELFHOST): FORCE $(WASM_TARGET) $(WASM_LINKER) $(WASM_RUNTIME)
	@bash scripts/build_wasm_linker_selfhost.sh build/wasm_linker_selfhost

wasm-linker-selfhost: $(WASM_LINKER_SELFHOST)

test-wasm-linker-selfhost: $(WASM_LINKER_SELFHOST) $(WASM_TARGET)
	@node tools/wasm_obj_linker/test_selfhost_api.mjs $(WASM_LINKER_SELFHOST)

test-wasm-js-pipeline: $(WASM_SELFHOST_API) $(WASM_LINKER_SELFHOST)
	@node tools/wasm_js_api/test_compile_link_pipeline.mjs $(WASM_SELFHOST_API) $(WASM_LINKER_SELFHOST)

test-wasm-js-e2e: $(WASM_SELFHOST_API) $(WASM_LINKER_SELFHOST) $(WASM_RUNTIME)
	@node tools/wasm_js_api/test_e2e_pipeline.mjs $(WASM_SELFHOST_API) $(WASM_LINKER_SELFHOST) --list-fail

check-tokenizer-perf-light:
	./scripts/check_tokenizer_perf_light.sh

log-tokenizer-hotpath-daily:
	./scripts/log_tokenizer_hotpath_daily.sh

test: $(TARGET) $(TEST_TOKENIZER) $(TEST_PARSER) $(TEST_E2E) $(TEST_PREPROCESS) $(TEST_FUZZ_QUICK) $(TEST_IR) $(TEST_FRAME_LAYOUT) $(TEST_IR_E2E) $(TEST_WASM32_BACKEND) $(TEST_WASM32_E2E) $(TEST_WASM32_OBJECT)
	$(TEST_TOKENIZER)
	$(TEST_PARSER)
	$(TEST_PREPROCESS)
	$(TEST_FUZZ_QUICK)
	$(TEST_IR)
	$(TEST_FRAME_LAYOUT)
	$(TEST_IR_E2E)
	$(TEST_WASM32_BACKEND)
	$(TEST_WASM32_E2E)
	$(TEST_WASM32_OBJECT)
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

# c-testsuite (test/external/c-testsuite, submodule) を ag_c で走らせて pass 率を可視化する。
# 初回は `git submodule update --init` で取得する必要がある。`make test` には含めない
# (失敗テスト多数のため別 target)。`make c-testsuite-verbose` で個別失敗一覧を出力。
c-testsuite: $(TARGET)
	@bash scripts/run_c_testsuite.sh

c-testsuite-verbose: $(TARGET)
	@bash scripts/run_c_testsuite.sh --verbose

FORCE:

.PHONY: test test-asan clean bench release check-tokenizer-perf-light log-tokenizer-hotpath-daily check-should-reject wasm32-object-fixture-scan wasm32-object-link-fixture-scan wasm32-object-link-all-fixture-scan wasm32-wat-fixture-scan wasm32-object-c-testsuite-scan wasm32-object-link-c-testsuite-scan wasm32-wat-c-testsuite-scan wasm32-scans test-wasm-obj-linker wasm-selfhost-api test-wasm-js-api wasm-linker-selfhost test-wasm-linker-selfhost test-wasm-js-pipeline test-wasm-js-e2e c-testsuite c-testsuite-verbose FORCE

-include $(DEPS)
