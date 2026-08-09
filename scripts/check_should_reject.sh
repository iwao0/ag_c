#!/bin/bash
# Verify that every should_reject fixture is invalid ISO C11 and is rejected
# by both the native and Wasm object compilers without an internal diagnostic.
set -u
cd "$(dirname "$0")/.."
. scripts/tool_timeout.sh

host_cc=${CC:-cc}
agc=${AG_C:-./build/ag_c}
agc_wasm=${AG_C_WASM:-./build/ag_c_wasm}
timeout_sec=${SHOULD_REJECT_TIMEOUT_SEC:-10}

if ! command -v "$host_cc" >/dev/null 2>&1; then
  echo "host C compiler not found: $host_cc" >&2
  exit 1
fi
if [ ! -x "$agc" ] || [ ! -x "$agc_wasm" ]; then
  echo "AG_C and AG_C_WASM must name executable compilers" >&2
  exit 1
fi
if ! validate_tool_timeout_sec "$timeout_sec"; then
  exit 1
fi

dir=test/fixtures/should_reject
if [ ! -d "$dir" ]; then
  echo "fixture directory not found: $dir" >&2
  exit 1
fi

total=0
host_accepted=0
host_timeout=0
native_rejected=0
native_missed=0
native_internal=0
native_timeout=0
wasm_rejected=0
wasm_missed=0
wasm_internal=0
wasm_timeout=0
for f in "$dir"/*.c; do
  [ -e "$f" ] || continue
  total=$((total + 1))
  run_with_timeout "$timeout_sec" \
    "$host_cc" -std=c11 -pedantic-errors -fsyntax-only \
    -Werror=implicit-function-declaration "$f" 2>/dev/null
  host_rc=$?
  if [ "$host_rc" -eq 124 ]; then
    host_timeout=$((host_timeout + 1))
    echo "HOST_TIMEOUT    $f"
  elif [ "$host_rc" -eq 0 ]; then
    host_accepted=$((host_accepted + 1))
    echo "HOST_ACCEPTED   $f"
  fi

  native_output=$(run_with_timeout "$timeout_sec" "$agc" "$f" 2>&1)
  native_rc=$?
  if [ "$native_rc" -eq 124 ]; then
    native_timeout=$((native_timeout + 1))
    echo "NATIVE_TIMEOUT  $f"
  elif [ "$native_rc" -eq 0 ]; then
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
    run_with_timeout "$timeout_sec" "$agc_wasm" -c -o /dev/null "$f" 2>&1
  )
  wasm_rc=$?
  if [ "$wasm_rc" -eq 124 ]; then
    wasm_timeout=$((wasm_timeout + 1))
    echo "WASM_TIMEOUT    $f"
  elif [ "$wasm_rc" -eq 0 ]; then
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
echo "should_reject summary: total=$total host_accepted=$host_accepted host_timeout=$host_timeout"
echo "native: rejected=$native_rejected missed=$native_missed internal=$native_internal timeout=$native_timeout"
echo "wasm:   rejected=$wasm_rejected missed=$wasm_missed internal=$wasm_internal timeout=$wasm_timeout"

if [ "$total" -eq 0 ] ||
   [ "$host_accepted" -ne 0 ] ||
   [ "$host_timeout" -ne 0 ] ||
   [ "$native_missed" -ne 0 ] ||
   [ "$native_internal" -ne 0 ] ||
   [ "$native_timeout" -ne 0 ] ||
   [ "$wasm_missed" -ne 0 ] ||
   [ "$wasm_internal" -ne 0 ] ||
   [ "$wasm_timeout" -ne 0 ]; then
  exit 1
fi

exit 0
