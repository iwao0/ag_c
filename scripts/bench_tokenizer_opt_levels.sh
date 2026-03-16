#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="${1:-/tmp/agc_tokenizer_bench}"
mkdir -p "$OUT_DIR"

run_with_opt() {
  local opt="$1"
  local out_file="$OUT_DIR/bench_${opt}.out"
  local cflags="-std=c11 -g ${opt} -Wall -Wextra"

  echo "[bench] building with CFLAGS='${cflags}'"
  make -C "$ROOT_DIR" clean >/dev/null
  make -C "$ROOT_DIR" bench CFLAGS="${cflags}" >"${out_file}"

  echo "[bench] ${opt} results"
  grep "^case=" "${out_file}" || true
}

run_with_opt "-O0"
run_with_opt "-O2"

echo "[bench] outputs: ${OUT_DIR}/bench_-O0.out, ${OUT_DIR}/bench_-O2.out"
