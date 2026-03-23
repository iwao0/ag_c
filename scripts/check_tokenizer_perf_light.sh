#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

tmp_out="$(mktemp /tmp/agc_tokenizer_perf_light.XXXXXX)"
trap 'rm -f "$tmp_out"' EXIT

make -j4 build/bench_tokenizer >/dev/null
build/bench_tokenizer >"$tmp_out"

# Reuse existing tokenizer perf regression check.
bash scripts/check_tokenizer_perf.sh "$tmp_out"

extract_hot_ops() {
  local name="$1"
  local line
  line="$(grep "hotpath=${name} " "$tmp_out" | tail -n 1 || true)"
  if [[ -z "$line" ]]; then
    echo "missing hotpath line: ${name}" >&2
    exit 1
  fi
  echo "$line" | sed -E 's/.*ops\/sec=([0-9]+).*/\1/'
}

check_hotpath() {
  local name="$1"
  local min_ops="$2"
  local ops
  ops="$(extract_hot_ops "$name")"
  echo "hotpath-check: ${name} ops/sec=${ops} min=${min_ops}"
  if [[ "$ops" -lt "$min_ops" ]]; then
    echo "hotpath regression (${name}): ${ops} < ${min_ops}" >&2
    exit 1
  fi
}

SCANNER_MIN_OPS="${SCANNER_MIN_OPS:-20000000}"
LITERALS_MIN_OPS="${LITERALS_MIN_OPS:-10000000}"
PUNCTUATOR_MIN_OPS="${PUNCTUATOR_MIN_OPS:-20000000}"

check_hotpath "scanner" "$SCANNER_MIN_OPS"
check_hotpath "literals" "$LITERALS_MIN_OPS"
check_hotpath "punctuator" "$PUNCTUATOR_MIN_OPS"

echo "tokenizer-perf-light: OK"

