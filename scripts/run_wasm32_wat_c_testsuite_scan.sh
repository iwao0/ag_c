#!/usr/bin/env bash
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
agc_wasm=${AG_C_WASM:-"$root/build/ag_c_wasm"}
suite=${C_TESTSUITE_DIR:-"$root/test/external/c-testsuite/tests/single-exec"}
out_dir=${WASM32_WAT_C_TESTSUITE_SCAN_DIR:-"$root/build/wasm32_wat_cts_scan"}
timeout_sec=${C_TESTSUITE_TIMEOUT_SEC:-10}
. "$root/scripts/c_testsuite_unsupported_cases.sh"
. "$root/scripts/tool_timeout.sh"
list_fail=0
verbose=0
validate=auto

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_wat_c_testsuite_scan.sh [--list-fail] [--verbose] [--no-validate]

Compiles test/external/c-testsuite/tests/single-exec/*.c with the Wasm WAT
backend, excluding unsupported GNU-extension cases. If wat2wasm is available,
converts WAT to a binary wasm module. If wasm-validate is available, validates
the generated module too.
Set AG_C_WASM to override the compiler path.
Set C_TESTSUITE_DIR to override the input directory.
Set WASM32_WAT_C_TESTSUITE_SCAN_DIR to override the output directory.
Set C_TESTSUITE_TIMEOUT_SEC to override the per-tool timeout.
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

if [ ! -x "$agc_wasm" ]; then
  echo "missing executable: $agc_wasm" >&2
  exit 2
fi

if [ ! -d "$suite" ]; then
  echo "missing c-testsuite directory: $suite" >&2
  exit 2
fi

if ! validate_c_testsuite_manifest "$suite"; then
  exit 2
fi

if ! validate_tool_timeout_sec "$timeout_sec"; then
  exit 2
fi

wat2wasm_available=0
if command -v wat2wasm >/dev/null 2>&1; then
  wat2wasm_available=1
fi

if [ "$validate" = "auto" ]; then
  if [ "$wat2wasm_available" -ne 0 ] && command -v wasm-validate >/dev/null 2>&1; then
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
timed_out=0

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
  wat="$out_dir/$base.wat"
  wasm="$out_dir/$base.wasm"
  err="$out_dir/$base.err"

  compile_status=0
  run_with_timeout "$timeout_sec" \
    "$agc_wasm" "$src" > "$wat" 2>"$err" || compile_status=$?
  if [ "$compile_status" -ne 0 ]; then
    failed=$((failed + 1))
    if [ "$compile_status" -eq 124 ]; then
      timed_out=$((timed_out + 1))
      msg="timed out after ${timeout_sec}s"
    else
      msg=$(sed -n '1p' "$err")
      [ -n "$msg" ] || msg="exited with status $compile_status"
    fi
    printf '%s\tcompile: %s\n' "$src" "$msg" >> "$failures"
    if [ "$verbose" -ne 0 ]; then
      printf 'FAIL %s\tcompile: %s\n' "$src" "$msg"
    fi
    continue
  fi

  if [ "$wat2wasm_available" -ne 0 ]; then
    wat2wasm_status=0
    run_with_timeout "$timeout_sec" \
      wat2wasm "$wat" -o "$wasm" 2>"$err" || wat2wasm_status=$?
    if [ "$wat2wasm_status" -ne 0 ]; then
      failed=$((failed + 1))
      if [ "$wat2wasm_status" -eq 124 ]; then
        timed_out=$((timed_out + 1))
        msg="timed out after ${timeout_sec}s"
      else
        msg=$(sed -n '1p' "$err")
        [ -n "$msg" ] || msg="exited with status $wat2wasm_status"
      fi
      printf '%s\twat2wasm: %s\n' "$src" "$msg" >> "$failures"
      if [ "$verbose" -ne 0 ]; then
        printf 'FAIL %s\twat2wasm: %s\n' "$src" "$msg"
      fi
      continue
    fi
  fi

  if [ "$validate" -ne 0 ]; then
    validate_status=0
    run_with_timeout "$timeout_sec" \
      wasm-validate "$wasm" >/dev/null 2>"$err" || validate_status=$?
    if [ "$validate_status" -ne 0 ]; then
      failed=$((failed + 1))
      if [ "$validate_status" -eq 124 ]; then
        timed_out=$((timed_out + 1))
        msg="timed out after ${timeout_sec}s"
      else
        msg=$(sed -n '1p' "$err")
        [ -n "$msg" ] || msg="exited with status $validate_status"
      fi
      printf '%s\tvalidate: %s\n' "$src" "$msg" >> "$failures"
      if [ "$verbose" -ne 0 ]; then
        printf 'FAIL %s\tvalidate: %s\n' "$src" "$msg"
      fi
      continue
    fi
  fi

  if [ "$verbose" -ne 0 ]; then
    printf 'PASS %s\n' "$src"
  fi
done

printf '==== wasm32 WAT c-testsuite scan ====\n'
printf 'Total:    %d\n' "$((scanned + skipped))"
printf 'Target:   %d\n' "$scanned"
printf 'Pass:     %d\n' "$((scanned - failed))"
printf 'Fail:     %d\n' "$failed"
printf 'Timeout:  %d\n' "$timed_out"
printf 'Skip:     %d\n' "$skipped"
printf 'Wat2wasm: %s\n' "$wat2wasm_available"
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
