#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out_dir=${1:-"$root/build/wasm_selfhost_api"}
obj_dir="$out_dir/obj"
list_file="$out_dir/objects.txt"
runtime_obj="$obj_dir/tools/wasm_obj_linker/runtime/libagc_runtime_js.o"
out_wasm="$out_dir/ag_c_wasm_api.wasm"
lock_dir="$out_dir.lock"

mkdir -p "$(dirname "$out_dir")"
while ! mkdir "$lock_dir" 2>/dev/null; do
  sleep 0.1
done
trap 'rmdir "$lock_dir"' EXIT
mkdir -p "$obj_dir"

make -C "$root" -j4 build/ag_c_wasm build/ag_wasm_link build/libagc_runtime.o
mkdir -p "$(dirname "$runtime_obj")"
(cd "$root" && AGC_SUPPRESS_WARNINGS=1 ./build/ag_c_wasm -c -o "$runtime_obj" \
  tools/wasm_obj_linker/runtime/libagc_runtime_js.c)

sources=()
while IFS= read -r src; do
  case "$src" in
    src/diag/messages_ja.c|src/diag/messages_en.c) continue ;;
  esac
  sources+=("$src")
done < <(cd "$root" && find src -name '*.c' -type f | sort)

: > "$list_file"
for src in "${sources[@]}"; do
  obj="$obj_dir/${src#src/}"
  obj="${obj%.c}.o"
  mkdir -p "$(dirname "$obj")"
  (cd "$root" && AGC_SUPPRESS_WARNINGS=1 ./build/ag_c_wasm -c -o "$obj" "$src")
  printf '%s\n' "$obj" >> "$list_file"
done

AGC_WASM_RUNTIME_OBJECT="$runtime_obj" "$root/build/ag_wasm_link" --no-entry \
  --stdio-write-import-module=env \
  --stdio-write-import-name=__agc_host_write \
  --initial-memory-pages=4096 \
  --maximum-memory-pages=4096 \
  --stack-size=16777216 \
  --export=agc_wasm_adapter_create \
  --export=agc_wasm_adapter_destroy \
  --export=agc_wasm_adapter_compile_wat \
  --export=agc_wasm_adapter_compile_object \
  --export=agc_wasm_adapter_compile_wat_named \
  --export=agc_wasm_adapter_compile_object_named \
  --export=agc_wasm_adapter_compile_wat_virtual \
  --export=agc_wasm_adapter_compile_object_virtual \
  --export=agc_wasm_adapter_set_diagnostic_locale \
  --export=agc_wasm_adapter_set_continuation_options \
  --export=agc_wasm_diagnostic_api_version \
  --export=agc_wasm_adapter_diagnostic_set_limits \
  --export=agc_wasm_adapter_diagnostic_count \
  --export=agc_wasm_adapter_diagnostic_bytes \
  --export=agc_wasm_adapter_diagnostic_limit_kind \
  --export=agc_wasm_adapter_diagnostic_severity \
  --export=agc_wasm_adapter_diagnostic_code_ptr \
  --export=agc_wasm_adapter_diagnostic_message_ptr \
  --export=agc_wasm_adapter_diagnostic_source_name_ptr \
  --export=agc_wasm_adapter_diagnostic_start_line \
  --export=agc_wasm_adapter_diagnostic_start_column \
  --export=agc_wasm_adapter_diagnostic_start_offset \
  --export=agc_wasm_adapter_diagnostic_end_line \
  --export=agc_wasm_adapter_diagnostic_end_column \
  --export=agc_wasm_adapter_diagnostic_end_offset \
  --export=__agc_runtime_stdout_ptr \
  --export=__agc_runtime_stdout_len \
  --export=__agc_runtime_stderr_ptr \
  --export=__agc_runtime_stderr_len \
  --export=__agc_runtime_stderr_reset \
  --export=__agc_runtime_termination_kind \
  --export=__agc_runtime_termination_status \
  --export=malloc \
  --export=free \
  -o "$out_wasm" $(cat "$list_file")

if command -v wasm-validate >/dev/null 2>&1; then
  wasm-validate "$out_wasm"
fi

printf '%s\n' "$out_wasm"
