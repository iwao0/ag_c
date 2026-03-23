#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root_dir"

# Tokenizer module outside (src/tokenizer/**) must not include tokenizer/internal headers.
if rg -n '#include "(\.\./)?tokenizer/internal/|#include "\.\./tokenizer/internal/' src --glob '!src/tokenizer/**' >/tmp/tokenizer_internal_boundary_violation.txt; then
  echo "[FAIL] tokenizer/internal headers are included outside tokenizer module."
  cat /tmp/tokenizer_internal_boundary_violation.txt
  exit 1
fi

echo "[OK] tokenizer/internal boundary check passed."
