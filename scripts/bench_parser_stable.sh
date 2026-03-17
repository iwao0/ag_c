#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-/tmp/agc_parser_bench_stable}"
RUNS="${2:-5}"

mkdir -p "$OUT_DIR"

if [[ "$RUNS" -lt 3 ]]; then
  echo "RUNS must be >= 3 (current: $RUNS)" >&2
  exit 2
fi

make -C "$ROOT_DIR" build/bench_parser >/dev/null

for i in $(seq 1 "$RUNS"); do
  "$ROOT_DIR"/build/bench_parser > "$OUT_DIR/run_${i}.out"
done

aggregate_case() {
  local case_name="$1"
  local input_size="$2"
  local metric="$3" # parser_MB/s or funcs/sec
  local metric_key="$metric"
  metric_key="${metric_key//\//_}"
  local tmp="$OUT_DIR/${case_name}_${input_size}_${metric_key}.vals"
  : > "$tmp"
  for i in $(seq 1 "$RUNS"); do
    local line
    line="$(grep "case=${case_name} input=${input_size}" "$OUT_DIR/run_${i}.out" | tail -n 1 || true)"
    if [[ -z "$line" ]]; then
      echo "missing line: case=${case_name} input=${input_size} in run_${i}.out" >&2
      exit 1
    fi
    if [[ "$metric" == "parser_MB/s" ]]; then
      echo "$line" | sed -E 's/.*parser_MB\/s=([0-9]+(\.[0-9]+)?).*/\1/' >> "$tmp"
    else
      echo "$line" | sed -E 's/.*funcs\/sec=([0-9]+).*/\1/' >> "$tmp"
    fi
  done

  sort -n "$tmp" > "${tmp}.sorted"
  local median_index=$(( (RUNS + 1) / 2 ))
  sed -n "${median_index}p" "${tmp}.sorted"
}

echo "Parser stable benchmark (median of ${RUNS} runs)"
echo "output_dir=${OUT_DIR}"
echo "case,input,median_parser_MB/s,median_funcs/sec"

for spec in \
  "mixed,262200b" \
  "expr-heavy,262176b" \
  "control-heavy,262185b"; do
  case_name="${spec%,*}"
  input_size="${spec#*,}"
  m_mbps="$(aggregate_case "$case_name" "$input_size" "parser_MB/s")"
  m_fps="$(aggregate_case "$case_name" "$input_size" "funcs/sec")"
  echo "${case_name},${input_size},${m_mbps},${m_fps}"
done
