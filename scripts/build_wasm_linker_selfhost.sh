#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out_dir=${1:-"$root/build/wasm_linker_selfhost"}
obj_dir="$out_dir/obj"
out_obj="$obj_dir/tools/wasm_obj_linker/ag_wasm_link.o"
runtime_obj="$obj_dir/tools/wasm_obj_linker/runtime/libagc_runtime_js.o"
out_wasm="$out_dir/ag_wasm_link.wasm"
lock_dir="$out_dir.lock"
lock_timeout_sec=${AGC_SELFHOST_LOCK_TIMEOUT_SEC:-60}
. "$root/scripts/tool_timeout.sh"

mkdir -p "$(dirname "$out_dir")"
validate_tool_timeout_sec "$lock_timeout_sec"
acquire_lock_dir "$lock_dir" "$lock_timeout_sec"
trap 'rmdir "$lock_dir"' EXIT
mkdir -p "$(dirname "$out_obj")"

make -C "$root" -j4 build/ag_c_wasm build/ag_wasm_link build/libagc_runtime.o

(cd "$root" && AGC_SUPPRESS_WARNINGS=1 ./build/ag_c_wasm -c -o "$out_obj" tools/wasm_obj_linker/ag_wasm_link.c)
mkdir -p "$(dirname "$runtime_obj")"
(cd "$root" && AGC_SUPPRESS_WARNINGS=1 ./build/ag_c_wasm -c -o "$runtime_obj" \
  tools/wasm_obj_linker/runtime/libagc_runtime_js.c)

AGC_WASM_RUNTIME_OBJECT="$runtime_obj" "$root/build/ag_wasm_link" --no-entry \
  --stdio-write-import-module=env \
  --stdio-write-import-name=__agc_host_write \
  --export=agc_wasm_link_objects \
  --export=agc_wasm_link_objects_with_options \
  --export=agc_wasm_link_objects_with_resource_limits \
  --export=agc_wasm_link_objects_with_export_signatures \
  --export=main \
  --export=__agc_runtime_stdout_ptr \
  --export=__agc_runtime_stdout_len \
  --export=__agc_runtime_stderr_ptr \
  --export=__agc_runtime_stderr_len \
  --export=__agc_runtime_stderr_reset \
  --export=__agc_runtime_termination_kind \
  --export=__agc_runtime_termination_status \
  --export=malloc \
  --export=free \
  -o "$out_wasm" "$out_obj"

if command -v wasm-validate >/dev/null 2>&1; then
  wasm-validate "$out_wasm"
fi

printf '%s\n' "$out_wasm"
