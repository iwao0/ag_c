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

line="$(grep 'input=262200b' "$file" | tail -n 1 || true)"
if [[ -z "$line" ]]; then
  echo "cannot find 256KB benchmark line in output" >&2
  exit 1
fi

tps="$(echo "$line" | sed -E 's/.*tokens\/sec=([0-9]+).*/\1/')"
alloc="$(echo "$line" | sed -E 's/.*alloc_count=([0-9]+).*/\1/')"

min_tps=1000000
max_alloc=1000

echo "perf-check: tokens/sec=${tps}, alloc_count=${alloc}"

if [[ "$tps" -lt "$min_tps" ]]; then
  echo "tokens/sec regression: ${tps} < ${min_tps}" >&2
  exit 1
fi

if [[ "$alloc" -gt "$max_alloc" ]]; then
  echo "alloc_count regression: ${alloc} > ${max_alloc}" >&2
  exit 1
fi

echo "perf-check: OK"
