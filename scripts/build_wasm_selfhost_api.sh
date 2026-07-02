#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out_dir=${1:-"$root/build/wasm_selfhost_api"}
obj_dir="$out_dir/obj"
list_file="$out_dir/objects.txt"
out_wasm="$out_dir/ag_c_wasm_api.wasm"

mkdir -p "$obj_dir"

make -C "$root" -j4 build/ag_c_wasm build/ag_wasm_link build/libagc_runtime.o

sources=(
  src/arch/arm64_apple.c
  src/arch/arm64_apple_ir.c
  src/arch/wasm32_ir.c
  src/arch/wasm32_obj.c
  src/codegen_emit.c
  src/config/config.c
  src/config/toml_reader.c
  src/diag/diag.c
  src/diag/error_catalog.c
  src/diag/messages_ja.c
  src/diag/ui_texts.c
  src/diag/warning_catalog.c
  src/ir/ir_alloc.c
  src/ir/ir_builder.c
  src/ir/ir_opt.c
  src/ir/ir_print.c
  src/ir/ir_regalloc.c
  src/main.c
  src/parser/alignas_value.c
  src/parser/anon_tag.c
  src/parser/arena.c
  src/parser/array_suffixes.c
  src/parser/config_runtime.c
  src/parser/decl.c
  src/parser/diag.c
  src/parser/enum_const.c
  src/parser/expr.c
  src/parser/loop_ctx.c
  src/parser/node_utils.c
  src/parser/parser.c
  src/parser/pragma_pack.c
  src/parser/semantic_ctx.c
  src/parser/stmt.c
  src/parser/struct_layout.c
  src/parser/switch_ctx.c
  src/preprocess/preprocess.c
  src/tokenizer/allocator.c
  src/tokenizer/config_runtime.c
  src/tokenizer/cursor.c
  src/tokenizer/escape.c
  src/tokenizer/filename_table.c
  src/tokenizer/keywords.c
  src/tokenizer/keywords_gperf.c
  src/tokenizer/literals.c
  src/tokenizer/number.c
  src/tokenizer/punctuator.c
  src/tokenizer/scanner.c
  src/tokenizer/token_kind.c
  src/tokenizer/tokenizer.c
  src/tokenizer/trigraph.c
)

: > "$list_file"
for src in "${sources[@]}"; do
  obj="$obj_dir/${src#src/}"
  obj="${obj%.c}.o"
  mkdir -p "$(dirname "$obj")"
  (cd "$root" && ./build/ag_c_wasm -c -o "$obj" "$src")
  printf '%s\n' "$obj" >> "$list_file"
done

"$root/build/ag_wasm_link" --no-entry \
  --export=agc_wasm_compile_wat \
  --export=malloc \
  --export=free \
  -o "$out_wasm" $(cat "$list_file")

if command -v wasm-validate >/dev/null 2>&1; then
  wasm-validate "$out_wasm"
fi

printf '%s\n' "$out_wasm"
