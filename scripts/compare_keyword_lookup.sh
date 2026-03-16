#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_BIN="${ROOT_DIR}/build/bench_keywords"

mkdir -p "${ROOT_DIR}/build"

cc -std=c11 -O2 -Wall -Wextra \
  "${ROOT_DIR}/test/bench_keywords.c" \
  "${ROOT_DIR}/src/tokenizer/keywords.c" \
  "${ROOT_DIR}/src/tokenizer/keywords_gperf.c" \
  -o "${OUT_BIN}"

"${OUT_BIN}"
