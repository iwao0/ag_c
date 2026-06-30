#!/usr/bin/env bash
set -u

agc_wasm=${AG_C_WASM:-./build/ag_c_wasm}
ag_wasm_link=${AG_WASM_LINK:-./build/ag_wasm_link}
out_dir=${WASM32_OBJECT_LINK_SCAN_DIR:-build/wasm32_obj_link_scan}
list_fail=0
verbose=0
fixture_source=e2e

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_object_link_fixture_scan.sh [--list-fail] [--verbose] [--all-fixtures]

Compiles fixtures in Wasm object mode, links each single-TU object with ag_wasm_link,
validates the linked wasm when wasm-validate is available, and runs it when
wasm-interp is available and the linked wasm has no imports.
By default, scans fixture paths registered in test/test_e2e.c.
Set AG_C_WASM / AG_WASM_LINK to override tool paths.
Set WASM32_OBJECT_LINK_SCAN_DIR to override the output directory.
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
    --all-fixtures)
      fixture_source=all
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

skip_reason() {
  case "$1" in
    test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c)
      echo "multi-TU link fixture component without main"
      ;;
    test/fixtures/probes_found_bugs/extern_global_got.c|\
    test/fixtures/probes_found_bugs/global_variadic_funcptr_call.c)
      echo "requires stdio runtime data imports (__stderrp/__stdoutp)"
      ;;
    *)
      return 1
      ;;
  esac
}

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

fixture_list="$out_dir/fixtures.txt"
if [ "$fixture_source" = "e2e" ]; then
  sed -n 's/.*"\(test\/fixtures\/[^"]*\.c\)".*/\1/p' test/test_e2e.c |
    LC_ALL=C sort -u > "$fixture_list"
else
  find test/fixtures -type f -name '*.c' | LC_ALL=C sort > "$fixture_list"
fi

scanned=0
failed=0
validated=0
runnable=0
ran=0
skipped=0
skipped_run_imports=0
skipped_run_tools=0

while IFS= read -r src; do
  case "$src" in
    */should_reject/*)
      continue
      ;;
  esac

  if reason=$(skip_reason "$src"); then
    skipped=$((skipped + 1))
    [ "$verbose" -ne 0 ] && printf 'SKIP %s\t%s\n' "$src" "$reason"
    continue
  fi

  scanned=$((scanned + 1))
  rel=${src#test/fixtures/}
  safe=${rel//\//__}
  obj="$out_dir/${safe%.c}.o"
  wasm="$out_dir/${safe%.c}.wasm"
  err="$out_dir/${safe%.c}.err"
  interp="$out_dir/${safe%.c}.interp"
  dump="$out_dir/${safe%.c}.objdump"

  if ! "$agc_wasm" -c -o "$obj" "$src" >/dev/null 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tcompile: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tcompile: %s\n' "$src" "$msg"
    continue
  fi

  if ! "$ag_wasm_link" --no-entry --export=main -o "$wasm" "$obj" >/dev/null 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\tlink: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tlink: %s\n' "$src" "$msg"
    continue
  fi

  if [ "$validate" -ne 0 ]; then
    if ! wasm-validate "$wasm" >/dev/null 2>"$err"; then
      failed=$((failed + 1))
      msg=$(sed -n '1p' "$err")
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

  if ! wasm-objdump -x "$wasm" > "$dump" 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
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
  if ! wasm-interp "$wasm" --run-all-exports > "$interp" 2>"$err"; then
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\trun: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\trun: %s\n' "$src" "$msg"
    continue
  fi

  if ! grep -q 'main() => i32:0' "$interp"; then
    failed=$((failed + 1))
    msg=$(tr '\n' ' ' < "$interp")
    printf '%s\tresult: %s\n' "$src" "$msg" >> "$failures"
    [ "$verbose" -ne 0 ] && printf 'FAIL %s\tresult: %s\n' "$src" "$msg"
    continue
  fi
  ran=$((ran + 1))
  [ "$verbose" -ne 0 ] && printf 'PASS %s\trun\n' "$src"
done < "$fixture_list"

printf '==== wasm32 object link fixture scan ====\n'
printf 'Source:           %s\n' "$fixture_source"
printf 'Total:            %d\n' "$scanned"
printf 'Pass:             %d\n' "$((scanned - failed))"
printf 'Fail:             %d\n' "$failed"
printf 'Skip:             %d\n' "$skipped"
printf 'Validate:         %s\n' "$validate"
printf 'Validated:        %d\n' "$validated"
printf 'Run tools:        %s\n' "$run"
printf 'Runnable:         %d\n' "$runnable"
printf 'Ran:              %d\n' "$ran"
printf 'Skip run imports: %d\n' "$skipped_run_imports"
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
