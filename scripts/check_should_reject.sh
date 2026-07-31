#!/bin/bash
# Verify that every should_reject fixture is invalid ISO C11 and is rejected
# by both the native and Wasm object compilers without an internal diagnostic.
set -u
cd "$(dirname "$0")/.."

if [ ! -x ./build/ag_c ] || [ ! -x ./build/ag_c_wasm ]; then
  echo "build/ag_c and build/ag_c_wasm are required" >&2
  exit 1
fi

dir=test/fixtures/should_reject
if [ ! -d "$dir" ]; then
  echo "fixture directory not found: $dir" >&2
  exit 1
fi

total=0
host_accepted=0
native_rejected=0
native_missed=0
native_internal=0
wasm_rejected=0
wasm_missed=0
wasm_internal=0
for f in "$dir"/*.c; do
  [ -e "$f" ] || continue
  total=$((total + 1))
  "${CC:-cc}" -std=c11 -pedantic-errors -fsyntax-only \
    -Werror=implicit-function-declaration "$f" 2>/dev/null
  host_rc=$?
  if [ "$host_rc" -eq 0 ]; then
    host_accepted=$((host_accepted + 1))
    echo "HOST_ACCEPTED   $f"
  fi

  native_output=$(./build/ag_c "$f" 2>&1)
  native_rc=$?
  if [ "$native_rc" -eq 0 ]; then
    native_missed=$((native_missed + 1))
    echo "NATIVE_ACCEPTED $f"
  else
    native_rejected=$((native_rejected + 1))
  fi
  case "$native_output" in
    *E0006*)
      native_internal=$((native_internal + 1))
      echo "NATIVE_INTERNAL $f"
      ;;
  esac

  wasm_output=$(
    ./build/ag_c_wasm -c -o /dev/null "$f" 2>&1
  )
  wasm_rc=$?
  if [ "$wasm_rc" -eq 0 ]; then
    wasm_missed=$((wasm_missed + 1))
    echo "WASM_ACCEPTED   $f"
  else
    wasm_rejected=$((wasm_rejected + 1))
  fi
  case "$wasm_output" in
    *E0006*)
      wasm_internal=$((wasm_internal + 1))
      echo "WASM_INTERNAL   $f"
      ;;
  esac
done

echo ""
echo "should_reject summary: total=$total host_accepted=$host_accepted"
echo "native: rejected=$native_rejected missed=$native_missed internal=$native_internal"
echo "wasm:   rejected=$wasm_rejected missed=$wasm_missed internal=$wasm_internal"

if [ "$total" -eq 0 ] ||
   [ "$host_accepted" -ne 0 ] ||
   [ "$native_missed" -ne 0 ] ||
   [ "$native_internal" -ne 0 ] ||
   [ "$wasm_missed" -ne 0 ] ||
   [ "$wasm_internal" -ne 0 ]; then
  exit 1
fi

exit 0
