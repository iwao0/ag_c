#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK_DIR="${1:-/tmp/agc_tokenizer_pgo}"
mkdir -p "${WORK_DIR}"

BASE_OUT="${WORK_DIR}/bench_o2.out"
GEN_OUT="${WORK_DIR}/bench_pgo_gen.out"
USE_OUT="${WORK_DIR}/bench_pgo_use.out"
PROF_DIR="${WORK_DIR}/profraw"
PROFDATA="${WORK_DIR}/merged.profdata"

find_llvm_profdata() {
  if command -v xcrun >/dev/null 2>&1; then
    local p
    p="$(xcrun --find llvm-profdata 2>/dev/null || true)"
    if [[ -n "${p}" ]]; then
      echo "${p}"
      return 0
    fi
  fi
  if command -v llvm-profdata >/dev/null 2>&1; then
    command -v llvm-profdata
    return 0
  fi
  return 1
}

echo "[pgo] baseline (-O2)"
make -C "${ROOT_DIR}" clean >/dev/null
make -C "${ROOT_DIR}" bench CFLAGS="-std=c11 -g -O2 -Wall -Wextra" >/dev/null
"${ROOT_DIR}/build/bench_tokenizer" > "${BASE_OUT}"

echo "[pgo] profile generate (-fprofile-generate)"
rm -rf "${PROF_DIR}"
mkdir -p "${PROF_DIR}"
make -C "${ROOT_DIR}" clean >/dev/null
make -C "${ROOT_DIR}" bench CFLAGS="-std=c11 -g -O2 -Wall -Wextra -fprofile-generate=${PROF_DIR}" >/dev/null
"${ROOT_DIR}/build/bench_tokenizer" > "${GEN_OUT}"

LLVM_PROFDATA_BIN="$(find_llvm_profdata)" || {
  echo "[pgo] llvm-profdata not found. skipping profile-use stage."
  exit 0
}
"${LLVM_PROFDATA_BIN}" merge -output="${PROFDATA}" "${PROF_DIR}"/*.profraw

echo "[pgo] profile use (-fprofile-use)"
make -C "${ROOT_DIR}" clean >/dev/null
make -C "${ROOT_DIR}" bench CFLAGS="-std=c11 -g -O2 -Wall -Wextra -fprofile-use=${PROFDATA}" >/dev/null
"${ROOT_DIR}/build/bench_tokenizer" > "${USE_OUT}"

echo "[pgo] result files:"
echo "  baseline: ${BASE_OUT}"
echo "  pgo-gen : ${GEN_OUT}"
echo "  pgo-use : ${USE_OUT}"
echo "[pgo] 256KB summary (mixed/ident/numeric/punct)"
grep "case=mixed input=262200b\\|case=ident input=262197b\\|case=numeric input=262160b\\|case=punct input=262272b" "${BASE_OUT}" | sed 's/^/[O2] /'
grep "case=mixed input=262200b\\|case=ident input=262197b\\|case=numeric input=262160b\\|case=punct input=262272b" "${USE_OUT}" | sed 's/^/[PGO] /'
