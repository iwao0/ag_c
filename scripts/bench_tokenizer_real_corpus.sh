#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CORPUS_FILE="${1:-/tmp/agc_tokenizer_corpus.c}"
OUT_FILE="${2:-/tmp/agc_tokenizer_corpus_bench.out}"

{
  find "${ROOT_DIR}/src" "${ROOT_DIR}/test" -type f \( -name "*.c" -o -name "*.h" \) | sort | while read -r f; do
    printf "\n/* ---- %s ---- */\n" "${f}"
    cat "${f}"
    printf "\n"
  done
} > "${CORPUS_FILE}"

echo "[corpus] generated: ${CORPUS_FILE} ($(wc -c < "${CORPUS_FILE}") bytes)"
make -C "${ROOT_DIR}" bench >/dev/null
TOKENIZER_BENCH_CORPUS_FILE="${CORPUS_FILE}" "${ROOT_DIR}/build/bench_tokenizer" | tee "${OUT_FILE}"
echo "[corpus] bench output: ${OUT_FILE}"
