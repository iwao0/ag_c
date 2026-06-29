#!/usr/bin/env bash
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
agc_wasm=${AG_C_WASM:-"$root/build/ag_c_wasm"}
suite=${C_TESTSUITE_DIR:-"$root/test/external/c-testsuite/tests/single-exec"}
out_dir=${WASM32_OBJECT_C_TESTSUITE_SCAN_DIR:-"$root/build/wasm32_obj_cts_scan"}
list_fail=0
verbose=0
validate=auto

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_object_c_testsuite_scan.sh [--list-fail] [--verbose] [--no-validate]

Compiles test/external/c-testsuite/tests/single-exec/*.c in Wasm object mode,
excluding unsupported GNU-extension cases. If wasm-validate is available,
validates each generated object too.
Set AG_C_WASM to override the compiler path.
Set C_TESTSUITE_DIR to override the input directory.
Set WASM32_OBJECT_C_TESTSUITE_SCAN_DIR to override the output directory.
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --list-fail)
      list_fail=1
      ;;
    --verbose)
      verbose=1
      ;;
    --no-validate)
      validate=0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

unsupported_reason() {
  case "$1" in
    00206) echo "GNU #pragma push_macro/pop_macro" ;;
    00216) echo "GNU empty struct / range designator" ;;
    *) return 1 ;;
  esac
}

if [ ! -x "$agc_wasm" ]; then
  echo "missing executable: $agc_wasm" >&2
  exit 2
fi

if [ ! -d "$suite" ]; then
  echo "missing c-testsuite directory: $suite" >&2
  exit 2
fi

if [ "$validate" = "auto" ]; then
  if command -v wasm-validate >/dev/null 2>&1; then
    validate=1
  else
    validate=0
  fi
fi

mkdir -p "$out_dir"
failures="$out_dir/failures.txt"
: > "$failures"

scanned=0
failed=0
skipped=0

for src in "$suite"/[0-9]*.c; do
  [ -f "$src" ] || continue
  base=$(basename "$src" .c)

  if reason=$(unsupported_reason "$base"); then
    skipped=$((skipped + 1))
    if [ "$verbose" -ne 0 ]; then
      printf 'SKIP %s\t%s\n' "$src" "$reason"
    fi
    continue
  fi

  scanned=$((scanned + 1))
  obj="$out_dir/$base.o"
  err="$out_dir/$base.err"

  if ! "$agc_wasm" -c -o "$obj" "$src" >/dev/null 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tcompile: %s\n' "$src" "$msg" >> "$failures"
    if [ "$verbose" -ne 0 ]; then
      printf 'FAIL %s\tcompile: %s\n' "$src" "$msg"
    fi
    continue
  fi

  if [ "$validate" -ne 0 ] && ! wasm-validate "$obj" >/dev/null 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tvalidate: %s\n' "$src" "$msg" >> "$failures"
    if [ "$verbose" -ne 0 ]; then
      printf 'FAIL %s\tvalidate: %s\n' "$src" "$msg"
    fi
    continue
  fi

  if [ "$verbose" -ne 0 ]; then
    printf 'PASS %s\n' "$src"
  fi
done

printf '==== wasm32 object c-testsuite scan ====\n'
printf 'Total:    %d\n' "$scanned"
printf 'Pass:     %d\n' "$((scanned - failed))"
printf 'Fail:     %d\n' "$failed"
printf 'Skip:     %d\n' "$skipped"
printf 'Validate: %s\n' "$validate"
printf 'Log:      %s\n' "$failures"

if [ "$failed" -ne 0 ]; then
  if [ "$list_fail" -ne 0 ]; then
    cat "$failures"
  else
    sed -n '1,20p' "$failures"
  fi
  exit 1
fi

exit 0
