#!/usr/bin/env bash
set -u

agc_wasm=${AG_C_WASM:-./build/ag_c_wasm}
out_dir=${WASM32_OBJECT_SCAN_DIR:-build/wasm32_obj_scan}
list_fail=0
verbose=0
validate=auto
fixture_source=all

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_object_fixture_scan.sh [--list-fail] [--verbose] [--no-validate] [--e2e-fixtures]

Compiles test/fixtures/**/*.c in Wasm object mode, excluding should_reject.
With --e2e-fixtures, compiles the fixture paths registered in test/test_e2e.c.
If wasm-validate is available, validates each generated object too.
Set AG_C_WASM to override the compiler path.
Set WASM32_OBJECT_SCAN_DIR to override the output directory.
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
    --e2e-fixtures)
      fixture_source=e2e
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

skip_reason() {
  case "$1" in
    test/fixtures/probes_found_bugs/complex_by_value_abi.c)
      echo "complex by-value ABI is not supported in Wasm object validation yet"
      ;;
    *)
      return 1
      ;;
  esac
}

fixture_list="$out_dir/fixtures.txt"
if [ "$fixture_source" = "e2e" ]; then
  sed -n 's/.*"\(test\/fixtures\/[^"]*\.c\)".*/\1/p' test/test_e2e.c |
    LC_ALL=C sort -u > "$fixture_list"
else
  find test/fixtures -type f -name '*.c' | LC_ALL=C sort > "$fixture_list"
fi

while IFS= read -r src; do
  case "$src" in
    */should_reject/*)
      continue
      ;;
  esac

  if reason=$(skip_reason "$src"); then
    skipped=$((skipped + 1))
    if [ "$verbose" -ne 0 ]; then
      printf 'SKIP %s\t%s\n' "$src" "$reason"
    fi
    continue
  fi

  scanned=$((scanned + 1))
  rel=${src#test/fixtures/}
  safe=${rel//\//__}
  obj="$out_dir/${safe%.c}.o"
  err="$out_dir/${safe%.c}.err"

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
done < "$fixture_list"

printf '==== wasm32 object fixture scan ====\n'
printf 'Source: %s\n' "$fixture_source"
printf 'Total: %d\n' "$scanned"
printf 'Pass:  %d\n' "$((scanned - failed))"
printf 'Fail:  %d\n' "$failed"
printf 'Skip:  %d\n' "$skipped"
printf 'Validate: %s\n' "$validate"
printf 'Log:   %s\n' "$failures"

if [ "$failed" -ne 0 ]; then
  if [ "$list_fail" -ne 0 ]; then
    cat "$failures"
  else
    sed -n '1,20p' "$failures"
  fi
  exit 1
fi

exit 0
