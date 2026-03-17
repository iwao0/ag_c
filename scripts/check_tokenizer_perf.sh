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
    | grep 'tokens/sec=' \
    | grep 'alloc_count=' \
    | tail -n 1 || true
}

extract_tps() {
  echo "$1" | sed -E 's/.*tokens\/sec=([0-9]+).*/\1/'
}

extract_alloc() {
  echo "$1" | sed -E 's/.*alloc_count=([0-9]+).*/\1/'
}

check_case() {
  local case_name="$1"
  local input_size="$2"
  local min_tps="$3"
  local max_alloc="$4"
  local line

  line="$(parse_line "$case_name" "$input_size")"
  if [[ -z "$line" ]]; then
    echo "cannot find benchmark line: case=${case_name} input=${input_size}" >&2
    exit 1
  fi

  local tps
  local alloc
  tps="$(extract_tps "$line")"
  alloc="$(extract_alloc "$line")"
  echo "perf-check: case=${case_name} tokens/sec=${tps}, alloc_count=${alloc}"

  if [[ "$tps" -lt "$min_tps" ]]; then
    echo "tokens/sec regression (${case_name}): ${tps} < ${min_tps}" >&2
    exit 1
  fi
  if [[ "$alloc" -gt "$max_alloc" ]]; then
    echo "alloc_count regression (${case_name}): ${alloc} > ${max_alloc}" >&2
    exit 1
  fi
}

# Case-specific thresholds (tunable via env for CI experiments)
MIXED_MIN_TPS="${MIXED_MIN_TPS:-3000000}"
IDENT_MIN_TPS="${IDENT_MIN_TPS:-2500000}"
NUMERIC_MIN_TPS="${NUMERIC_MIN_TPS:-2500000}"
PUNCT_MIN_TPS="${PUNCT_MIN_TPS:-8000000}"
MIXED_MAX_ALLOC="${MIXED_MAX_ALLOC:-1000}"
IDENT_MAX_ALLOC="${IDENT_MAX_ALLOC:-400}"
NUMERIC_MAX_ALLOC="${NUMERIC_MAX_ALLOC:-700}"
PUNCT_MAX_ALLOC="${PUNCT_MAX_ALLOC:-500}"

check_case "mixed" "262200b" "$MIXED_MIN_TPS" "$MIXED_MAX_ALLOC"
check_case "ident" "262197b" "$IDENT_MIN_TPS" "$IDENT_MAX_ALLOC"
check_case "numeric" "262160b" "$NUMERIC_MIN_TPS" "$NUMERIC_MAX_ALLOC"
check_case "punct" "262272b" "$PUNCT_MIN_TPS" "$PUNCT_MAX_ALLOC"

echo "perf-check: OK"
