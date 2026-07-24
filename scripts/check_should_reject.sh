#!/bin/bash
# test/fixtures/should_reject/*.c を ag_c に渡し、
# 「ag_c が受け入れてしまった (本来 cc が拒否する)」ものをレポートする。
#
# このスクリプトは main の test に gating されない (CI red にしない)。
# 各 fixture は将来 ag_c が拒否するようになったときの target を示すドキュメントを
# 兼ねていて、コミット時点で見えている MISSED 件数を可視化する。
set -u
cd "$(dirname "$0")/.."

if [ ! -x ./build/ag_c ]; then
  echo "build/ag_c not found; run 'make build/ag_c' first" >&2
  exit 1
fi

dir=test/fixtures/should_reject
if [ ! -d "$dir" ]; then
  echo "fixture directory not found: $dir" >&2
  exit 1
fi

total=0
missed=0
ok_reject=0
spurious=0
for f in "$dir"/*.c; do
  [ -e "$f" ] || continue
  total=$((total + 1))
  # ISO C11の制約違反と「暗黙宣言」をいずれもerrorとして比較する。
  cc -std=c11 -pedantic-errors -fsyntax-only \
    -Werror=implicit-function-declaration "$f" 2>/dev/null
  cc_rc=$?
  ./build/ag_c "$f" >/dev/null 2>&1
  agc_rc=$?
  if [ $cc_rc -ne 0 ] && [ $agc_rc -eq 0 ]; then
    missed=$((missed + 1))
    echo "MISSED  $f"
  elif [ $cc_rc -ne 0 ] && [ $agc_rc -ne 0 ]; then
    ok_reject=$((ok_reject + 1))
  elif [ $cc_rc -eq 0 ] && [ $agc_rc -ne 0 ]; then
    spurious=$((spurious + 1))
    echo "SPURIOUS $f (ag_c rejects valid C)"
  fi
done

echo ""
echo "should_reject summary: total=$total  rejected_by_agc=$ok_reject  missed=$missed  spurious=$spurious"
# missed や spurious があっても exit 0 (CI を red にしない)。
# 修正したい/対応進捗を見たい場合は手動で実行する想定。
exit 0
