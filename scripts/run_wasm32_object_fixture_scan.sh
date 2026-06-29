#!/usr/bin/env bash
set -u

agc_wasm=${AG_C_WASM:-./build/ag_c_wasm}
out_dir=${WASM32_OBJECT_SCAN_DIR:-build/wasm32_obj_scan}
list_fail=0
verbose=0

usage() {
  cat <<'EOF'
usage: scripts/run_wasm32_object_fixture_scan.sh [--list-fail] [--verbose]

Compiles test/fixtures/**/*.c in Wasm object mode, excluding should_reject.
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

mkdir -p "$out_dir"
failures="$out_dir/failures.txt"
: > "$failures"

scanned=0
failed=0

while IFS= read -r src; do
  case "$src" in
    */should_reject/*)
      continue
      ;;
  esac

  scanned=$((scanned + 1))
  rel=${src#test/fixtures/}
  safe=${rel//\//__}
  obj="$out_dir/${safe%.c}.o"
  err="$out_dir/${safe%.c}.err"

  if "$agc_wasm" -c -o "$obj" "$src" >/dev/null 2>"$err"; then
    if [ "$verbose" -ne 0 ]; then
      printf 'PASS %s\n' "$src"
    fi
  else
    failed=$((failed + 1))
    msg=$(sed -n '1p' "$err")
    printf '%s\t%s\n' "$src" "$msg" >> "$failures"
    if [ "$verbose" -ne 0 ]; then
      printf 'FAIL %s\t%s\n' "$src" "$msg"
    fi
  fi
done < <(find test/fixtures -type f -name '*.c' | LC_ALL=C sort)

printf '==== wasm32 object fixture scan ====\n'
printf 'Total: %d\n' "$scanned"
printf 'Pass:  %d\n' "$((scanned - failed))"
printf 'Fail:  %d\n' "$failed"
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
