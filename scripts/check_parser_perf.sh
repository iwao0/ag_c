#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <bench-output-file>" >&2
  exit 2
fi

file="$1"
if [[ ! -f "$file" ]]; then
  echo "bench output file not found: $file" >&2
  exit 2
fi

parse_line() {
  local case_name="$1"
  local input_size="$2"
  grep "case=${case_name} input=${input_size}" "$file" \
    | grep 'parser_MB/s=' \
    | grep 'funcs/sec=' \
    | tail -n 1 || true
}

extract_parser_mbps() {
  echo "$1" | sed -E 's/.*parser_MB\/s=([0-9]+(\.[0-9]+)?).*/\1/'
}

extract_funcs_per_sec() {
  echo "$1" | sed -E 's/.*funcs\/sec=([0-9]+).*/\1/'
}

check_case() {
  local case_name="$1"
  local input_size="$2"
  local min_parser_mbps="$3"
  local min_funcs_per_sec="$4"
  local line

  line="$(parse_line "$case_name" "$input_size")"
  if [[ -z "$line" ]]; then
    echo "cannot find parser benchmark line: case=${case_name} input=${input_size}" >&2
    exit 1
  fi

  local parser_mbps
  local funcs_per_sec
  parser_mbps="$(extract_parser_mbps "$line")"
  funcs_per_sec="$(extract_funcs_per_sec "$line")"
  echo "parser-perf-check: case=${case_name} parser_MB/s=${parser_mbps}, funcs/sec=${funcs_per_sec}"

  awk -v val="$parser_mbps" -v min="$min_parser_mbps" 'BEGIN { exit (val + 0 >= min + 0) ? 0 : 1 }' \
    || { echo "parser_MB/s regression (${case_name}): ${parser_mbps} < ${min_parser_mbps}" >&2; exit 1; }

  if [[ "$funcs_per_sec" -lt "$min_funcs_per_sec" ]]; then
    echo "funcs/sec regression (${case_name}): ${funcs_per_sec} < ${min_funcs_per_sec}" >&2
    exit 1
  fi
}

# Case-specific thresholds (tunable via env for CI experiments)
MIXED_256_MIN_MBPS="${MIXED_256_MIN_MBPS:-20}"
EXPR_256_MIN_MBPS="${EXPR_256_MIN_MBPS:-20}"
CONTROL_256_MIN_MBPS="${CONTROL_256_MIN_MBPS:-25}"
MIXED_256_MIN_FPS="${MIXED_256_MIN_FPS:-300000}"
EXPR_256_MIN_FPS="${EXPR_256_MIN_FPS:-350000}"
CONTROL_256_MIN_FPS="${CONTROL_256_MIN_FPS:-300000}"

check_case "mixed" "262200b" "$MIXED_256_MIN_MBPS" "$MIXED_256_MIN_FPS"
check_case "expr-heavy" "262176b" "$EXPR_256_MIN_MBPS" "$EXPR_256_MIN_FPS"
check_case "control-heavy" "262185b" "$CONTROL_256_MIN_MBPS" "$CONTROL_256_MIN_FPS"

echo "parser-perf-check: OK"
