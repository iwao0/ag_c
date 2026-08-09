#!/usr/bin/env bash
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
agc_wasm=${AG_C_WASM:-"$root/build/ag_c_wasm"}
ag_wasm_link=${AG_WASM_LINK:-"$root/build/ag_wasm_link"}
suite=${C_TESTSUITE_DIR:-"$root/test/external/c-testsuite/tests/single-exec"}
out_dir=${WASM32_OBJECT_LINK_C_TESTSUITE_SCAN_DIR:-"$root/build/wasm32_obj_link_cts_scan"}
tool_timeout_sec=${C_TESTSUITE_TIMEOUT_SEC:-10}
interp_timeout_sec=${WASM32_OBJECT_LINK_SCAN_TIMEOUT_SEC:-60}
. "$root/scripts/c_testsuite_unsupported_cases.sh"
list_fail=0
verbose=0

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_object_link_c_testsuite_scan.sh [--list-fail] [--verbose]

Compiles c-testsuite single-exec cases in Wasm object mode, links each object
with ag_wasm_link, validates the linked wasm when wasm-validate is available,
and runs it when wasm-interp/wasm-objdump are available and the linked wasm has
no imports. Unsupported GNU-extension cases are skipped.
Set AG_C_WASM / AG_WASM_LINK to override tool paths.
Set C_TESTSUITE_DIR to override the input directory.
Set WASM32_OBJECT_LINK_C_TESTSUITE_SCAN_DIR to override the output directory.
Set C_TESTSUITE_TIMEOUT_SEC to override the compile/link/tool timeout.
Set WASM32_OBJECT_LINK_SCAN_TIMEOUT_SEC to override the wasm-interp timeout.
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

if [ ! -x "$ag_wasm_link" ]; then
  echo "missing executable: $ag_wasm_link" >&2
  exit 2
fi

if [ ! -d "$suite" ]; then
  echo "missing c-testsuite directory: $suite" >&2
  exit 2
fi

if ! validate_c_testsuite_manifest "$suite"; then
  exit 2
fi

if ! validate_c_testsuite_timeout_sec "$tool_timeout_sec" ||
   ! validate_c_testsuite_timeout_sec "$interp_timeout_sec"; then
  exit 2
fi

validate=0
if command -v wasm-validate >/dev/null 2>&1; then
  validate=1
fi

run=0
if command -v wasm-interp >/dev/null 2>&1 && command -v wasm-objdump >/dev/null 2>&1; then
  run=1
fi

mkdir -p "$out_dir"
failures="$out_dir/failures.txt"
: > "$failures"

scanned=0
failed=0
skipped=0
timed_out=0
validated=0
runnable=0
ran=0
skipped_run_imports=0
skipped_run_tools=0
skipped_run_params=0

for src in "$suite"/[0-9]*.c; do
  [ -f "$src" ] || continue
  base=$(basename "$src" .c)

  if reason=$(unsupported_reason "$base"); then
    skipped=$((skipped + 1))
    [ "$verbose" -ne 0 ] && printf 'SKIP %s\t%s\n' "$src" "$reason"
    continue
  fi

  scanned=$((scanned + 1))
  obj="$out_dir/$base.o"
  wasm="$out_dir/$base.wasm"
  err="$out_dir/$base.err"
  dump="$out_dir/$base.objdump"
  interp="$out_dir/$base.interp"

  compile_status=0
  c_testsuite_run_with_timeout "$tool_timeout_sec" \
    "$agc_wasm" -c -o "$obj" "$src" >/dev/null 2>"$err" || compile_status=$?
  if [ "$compile_status" -ne 0 ]; then
    failed=$((failed + 1))
    if [ "$compile_status" -eq 124 ]; then
      timed_out=$((timed_out + 1))
      msg="timed out after ${tool_timeout_sec}s"
    else
      msg=$(sed -n '1p' "$err")
      [ -n "$msg" ] || msg="exited with status $compile_status"
    fi
    printf '%s\tcompile: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tcompile: %s\n' "$src" "$msg"
    continue
  fi

  link_status=0
  c_testsuite_run_with_timeout "$tool_timeout_sec" \
    "$ag_wasm_link" --no-entry --export=main -o "$wasm" "$obj" \
    >/dev/null 2>"$err" || link_status=$?
  if [ "$link_status" -ne 0 ]; then
    failed=$((failed + 1))
    if [ "$link_status" -eq 124 ]; then
      timed_out=$((timed_out + 1))
      msg="timed out after ${tool_timeout_sec}s"
    else
      msg=$(sed -n '1p' "$err")
      [ -n "$msg" ] || msg="exited with status $link_status"
    fi
    printf '%s\tlink: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tlink: %s\n' "$src" "$msg"
    continue
  fi

  if [ "$validate" -ne 0 ]; then
    validate_status=0
    c_testsuite_run_with_timeout "$tool_timeout_sec" \
      wasm-validate "$wasm" >/dev/null 2>"$err" || validate_status=$?
    if [ "$validate_status" -ne 0 ]; then
      failed=$((failed + 1))
      if [ "$validate_status" -eq 124 ]; then
        timed_out=$((timed_out + 1))
        msg="timed out after ${tool_timeout_sec}s"
      else
        msg=$(sed -n '1p' "$err")
        [ -n "$msg" ] || msg="exited with status $validate_status"
      fi
      printf '%s\tvalidate: %s\n' "$src" "$msg" >> "$failures"
      [ "$verbose" -ne 0 ] && printf 'FAIL %s\tvalidate: %s\n' "$src" "$msg"
      continue
    fi
    validated=$((validated + 1))
  fi

  if [ "$run" -eq 0 ]; then
    skipped_run_tools=$((skipped_run_tools + 1))
    [ "$verbose" -ne 0 ] && printf 'PASS %s\tlink-only\n' "$src"
    continue
  fi

  objdump_status=0
  c_testsuite_run_with_timeout "$tool_timeout_sec" \
    wasm-objdump -x "$wasm" > "$dump" 2>"$err" || objdump_status=$?
  if [ "$objdump_status" -ne 0 ]; then
    failed=$((failed + 1))
    if [ "$objdump_status" -eq 124 ]; then
      timed_out=$((timed_out + 1))
      msg="timed out after ${tool_timeout_sec}s"
    else
      msg=$(sed -n '1p' "$err")
      [ -n "$msg" ] || msg="exited with status $objdump_status"
    fi
    printf '%s\tobjdump: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tobjdump: %s\n' "$src" "$msg"
    continue
  fi

  if grep -q '^Import\[' "$dump"; then
    skipped_run_imports=$((skipped_run_imports + 1))
    [ "$verbose" -ne 0 ] && printf 'PASS %s\tlink-only-imports\n' "$src"
    continue
  fi

  runnable=$((runnable + 1))
  interp_status=0
  c_testsuite_run_with_timeout "$interp_timeout_sec" \
    wasm-interp "$wasm" --run-all-exports > "$interp" 2>"$err" ||
    interp_status=$?
  if [ "$interp_status" -ne 0 ]; then
    failed=$((failed + 1))
    if [ "$interp_status" -eq 124 ]; then
      timed_out=$((timed_out + 1))
      msg="wasm-interp timed out after ${interp_timeout_sec}s"
    else
      msg=$(sed -n '1p' "$err")
      [ -n "$msg" ] || msg="wasm-interp exited with status $interp_status"
    fi
    printf '%s\trun: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\trun: %s\n' "$src" "$msg"
    continue
  fi

  if ! grep -q 'main() => i32:0' "$interp"; then
    if [ ! -s "$interp" ]; then
      skipped_run_params=$((skipped_run_params + 1))
      [ "$verbose" -ne 0 ] && printf 'PASS %s\tlink-only-params\n' "$src"
      continue
    fi
    failed=$((failed + 1))
    msg=$(tr '\n' ' ' < "$interp")
    printf '%s\tresult: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tresult: %s\n' "$src" "$msg"
    continue
  fi
  ran=$((ran + 1))
  [ "$verbose" -ne 0 ] && printf 'PASS %s\trun\n' "$src"
done

printf '==== wasm32 object link c-testsuite scan ====\n'
printf 'Total:            %d\n' "$((scanned + skipped))"
printf 'Target:           %d\n' "$scanned"
printf 'Pass:             %d\n' "$((scanned - failed))"
printf 'Fail:             %d\n' "$failed"
printf 'Timeout:          %d\n' "$timed_out"
printf 'Skip:             %d\n' "$skipped"
printf 'Validate:         %s\n' "$validate"
printf 'Validated:        %d\n' "$validated"
printf 'Run tools:        %s\n' "$run"
printf 'Runnable:         %d\n' "$runnable"
printf 'Ran:              %d\n' "$ran"
printf 'Skip run imports: %d\n' "$skipped_run_imports"
printf 'Skip run params:  %d\n' "$skipped_run_params"
printf 'Skip run tools:   %d\n' "$skipped_run_tools"
printf 'Log:              %s\n' "$failures"

if [ "$failed" -ne 0 ]; then
  if [ "$list_fail" -ne 0 ]; then
    cat "$failures"
  else
    sed -n '1,20p' "$failures"
  fi
  exit 1
fi

exit 0
