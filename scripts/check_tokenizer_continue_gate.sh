#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <bench-output-file> <corpus-output-file>" >&2
  exit 2
fi

bench_file="$1"
corpus_file="$2"

if [[ ! -f "$bench_file" ]]; then
  echo "bench output file not found: $bench_file" >&2
  exit 2
fi
if [[ ! -f "$corpus_file" ]]; then
  echo "corpus output file not found: $corpus_file" >&2
  exit 2
fi

# Phase0 baseline (2026-03-16)
# Override by env if needed.
BASE_MIXED_TPS="${BASE_MIXED_TPS:-19705021}"
BASE_IDENT_TPS="${BASE_IDENT_TPS:-12289547}"
BASE_NUMERIC_TPS="${BASE_NUMERIC_TPS:-13075812}"
BASE_PUNCT_TPS="${BASE_PUNCT_TPS:-33759919}"
BASE_CORPUS_TPS="${BASE_CORPUS_TPS:-16561884}"

BASE_MIXED_ALLOC="${BASE_MIXED_ALLOC:-21}"
BASE_IDENT_ALLOC="${BASE_IDENT_ALLOC:-6}"
BASE_NUMERIC_ALLOC="${BASE_NUMERIC_ALLOC:-15}"
BASE_PUNCT_ALLOC="${BASE_PUNCT_ALLOC:-10}"
BASE_CORPUS_ALLOC="${BASE_CORPUS_ALLOC:-7}"

parse_line() {
  local file="$1"
  local case_name="$2"
  local input_size="$3"
  grep "case=${case_name} input=${input_size}" "$file" | tail -n 1 || true
}

extract_tps() {
  echo "$1" | sed -E 's/.*tokens\/sec=([0-9]+).*/\1/'
}

extract_alloc() {
  echo "$1" | sed -E 's/.*alloc_count=([0-9]+).*/\1/'
}

check_case() {
  local file="$1"
  local case_name="$2"
  local input_size="$3"
  local base_tps="$4"
  local base_alloc="$5"

  local line
  line="$(parse_line "$file" "$case_name" "$input_size")"
  if [[ -z "$line" ]]; then
    echo "missing benchmark line: case=${case_name} input=${input_size} in ${file}" >&2
    exit 1
  fi

  local tps alloc min_tps
  tps="$(extract_tps "$line")"
  alloc="$(extract_alloc "$line")"
  min_tps=$(( base_tps * 95 / 100 ))

  echo "gate-check: case=${case_name} tps=${tps} (baseline=${base_tps}, min=${min_tps}) alloc=${alloc} (baseline=${base_alloc})"

  if [[ "$tps" -lt "$min_tps" ]]; then
    echo "gate NG: ${case_name} tokens/sec dropped more than 5%" >&2
    exit 1
  fi
  if [[ "$alloc" -gt "$base_alloc" ]]; then
    echo "gate NG: ${case_name} alloc_count increased (${alloc} > ${base_alloc})" >&2
    exit 1
  fi
}

check_case "$bench_file" "mixed" "262200b" "$BASE_MIXED_TPS" "$BASE_MIXED_ALLOC"
check_case "$bench_file" "ident" "262197b" "$BASE_IDENT_TPS" "$BASE_IDENT_ALLOC"
check_case "$bench_file" "numeric" "262160b" "$BASE_NUMERIC_TPS" "$BASE_NUMERIC_ALLOC"
check_case "$bench_file" "punct" "262272b" "$BASE_PUNCT_TPS" "$BASE_PUNCT_ALLOC"
check_case "$corpus_file" "corpus" "211541b" "$BASE_CORPUS_TPS" "$BASE_CORPUS_ALLOC"

echo "gate-check: OK (all cases within -5% and alloc_count non-increasing)"
