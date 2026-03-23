#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

out_csv="${1:-docs/init_c11_compiler/tokenizer_hotpath_daily.csv}"
tmp_out="$(mktemp /tmp/agc_tokenizer_hotpath_daily.XXXXXX)"
trap 'rm -f "$tmp_out"' EXIT

make -j4 build/bench_tokenizer >/dev/null
build/bench_tokenizer >"$tmp_out"

extract_case_tps() {
  local case_name="$1"
  grep "case=${case_name} input=262" "$tmp_out" | tail -n 1 | sed -E 's/.*tokens\/sec=([0-9]+).*/\1/'
}

extract_hot_ops() {
  local name="$1"
  grep "hotpath=${name} " "$tmp_out" | tail -n 1 | sed -E 's/.*ops\/sec=([0-9]+).*/\1/'
}

extract_case_alloc() {
  local case_name="$1"
  grep "case=${case_name} input=262" "$tmp_out" | tail -n 1 | sed -E 's/.*alloc_count=([0-9]+).*/\1/'
}

today="$(date +%F)"
mixed_tps="$(extract_case_tps mixed)"
ident_tps="$(extract_case_tps ident)"
numeric_tps="$(extract_case_tps numeric)"
punct_tps="$(extract_case_tps punct)"
mixed_alloc="$(extract_case_alloc mixed)"
scanner_ops="$(extract_hot_ops scanner)"
literals_ops="$(extract_hot_ops literals)"
punctuator_ops="$(extract_hot_ops punctuator)"

if [[ ! -f "$out_csv" ]]; then
  mkdir -p "$(dirname "$out_csv")"
  echo "date,mixed_tps,ident_tps,numeric_tps,punct_tps,mixed_alloc,scanner_ops,literals_ops,punctuator_ops" >"$out_csv"
fi

echo "${today},${mixed_tps},${ident_tps},${numeric_tps},${punct_tps},${mixed_alloc},${scanner_ops},${literals_ops},${punctuator_ops}" >>"$out_csv"
echo "appended: ${out_csv} (${today})"

