#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out_dir=${1:-"$root/build/wasm_linker_selfhost"}
obj_dir="$out_dir/obj"
out_obj="$obj_dir/tools/wasm_obj_linker/ag_wasm_link.o"
out_wasm="$out_dir/ag_wasm_link.wasm"

mkdir -p "$(dirname "$out_obj")"

make -C "$root" -j4 build/ag_c_wasm build/ag_wasm_link build/libagc_runtime.o

(cd "$root" && AGC_SUPPRESS_WARNINGS=1 ./build/ag_c_wasm -c -o "$out_obj" tools/wasm_obj_linker/ag_wasm_link.c)

"$root/build/ag_wasm_link" --no-entry \
  --export=agc_wasm_link_objects \
  --export=main \
  --export=malloc \
  --export=free \
  -o "$out_wasm" "$out_obj"

if command -v wasm-validate >/dev/null 2>&1; then
  wasm-validate "$out_wasm"
fi

printf '%s\n' "$out_wasm"
