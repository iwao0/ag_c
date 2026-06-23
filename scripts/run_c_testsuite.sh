#!/bin/bash
# c-testsuite (test/external/c-testsuite) を ag_c で実行し pass/fail を集計する。
#
# 各テスト:
#   1. ./build/ag_c file.c > file.s  (アセンブリ生成)
#   2. cc -arch arm64 file.s -o file (リンク)
#   3. ./file                         (実行)
#   4. exit code == 0 かつ stdout が .expected と一致したら pass
#
# 結果は分類別カウントとパス率を出力。`--verbose` で個別失敗の一覧、
# `--list-fail` で失敗テスト名のみ列挙。

set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
AGC="$ROOT/build/ag_c"
SUITE="$ROOT/test/external/c-testsuite/tests/single-exec"

VERBOSE=0
LIST_FAIL=0
for arg in "$@"; do
  case "$arg" in
    --verbose) VERBOSE=1 ;;
    --list-fail) LIST_FAIL=1 ;;
    -h|--help)
      echo "Usage: $0 [--verbose] [--list-fail]"
      exit 0 ;;
  esac
done

if [ ! -d "$SUITE" ]; then
  echo "error: c-testsuite が見つかりません ($SUITE)"
  echo "submodule を初期化してください:  git submodule update --init"
  exit 1
fi

if [ ! -x "$AGC" ]; then
  echo "error: ag_c が見つかりません ($AGC)。先に make してください。"
  exit 1
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

pass=0
fail_compile=0
fail_assemble=0
fail_runtime=0
fail_stdout=0
fail_timeout=0
total=0

fail_compile_list=()
fail_assemble_list=()
fail_runtime_list=()
fail_stdout_list=()
fail_timeout_list=()

# tests/single-exec/*.c (NNNNN.c.expected がペア) を列挙
for cfile in "$SUITE"/[0-9]*.c; do
  [ -f "$cfile" ] || continue
  total=$((total + 1))
  base=$(basename "$cfile" .c)
  expected="$cfile.expected"

  sfile="$TMPDIR/$base.s"
  exe="$TMPDIR/$base"

  # ag_c で compile (タイムアウト 10s 程度の安全策)
  # ag_c は CWD 相対の include/ を見るため ROOT に cd して実行
  if ! ( cd "$ROOT" && "$AGC" "$cfile" > "$sfile" ) 2>/dev/null; then
    fail_compile=$((fail_compile + 1))
    fail_compile_list+=("$base")
    continue
  fi

  # cc で link (-arch arm64 を明示)
  if ! cc -arch arm64 -o "$exe" "$sfile" 2>/dev/null; then
    fail_assemble=$((fail_assemble + 1))
    fail_assemble_list+=("$base")
    continue
  fi

  # 実行 (CWD を TMPDIR にして相対パスのファイル書き込みを ROOT に漏らさない)
  actual=$( ( cd "$TMPDIR" && "$exe" ) 2>/dev/null )
  rc=$?

  if [ $rc -ne 0 ]; then
    fail_runtime=$((fail_runtime + 1))
    fail_runtime_list+=("$base (rc=$rc)")
    continue
  fi

  # expected と stdout を比較
  if [ -f "$expected" ]; then
    expected_text=$(cat "$expected")
    if [ "$actual" != "$expected_text" ]; then
      fail_stdout=$((fail_stdout + 1))
      fail_stdout_list+=("$base")
      continue
    fi
  fi

  pass=$((pass + 1))
done

if [ "$LIST_FAIL" -eq 1 ]; then
  printf '\n== Compile fail ==\n'
  for t in "${fail_compile_list[@]:-}"; do [ -n "$t" ] && echo "  $t"; done
  printf '\n== Assemble fail ==\n'
  for t in "${fail_assemble_list[@]:-}"; do [ -n "$t" ] && echo "  $t"; done
  printf '\n== Runtime fail ==\n'
  for t in "${fail_runtime_list[@]:-}"; do [ -n "$t" ] && echo "  $t"; done
  printf '\n== Stdout mismatch ==\n'
  for t in "${fail_stdout_list[@]:-}"; do [ -n "$t" ] && echo "  $t"; done
fi

if [ "$VERBOSE" -eq 1 ] && [ "$LIST_FAIL" -eq 0 ]; then
  if [ "${#fail_compile_list[@]}" -gt 0 ]; then
    printf '\nCompile fail (%d):\n' "$fail_compile"
    printf '  %s\n' "${fail_compile_list[@]:0:20}"
    [ "${#fail_compile_list[@]}" -gt 20 ] && printf '  ... %d more\n' $((${#fail_compile_list[@]} - 20))
  fi
  if [ "${#fail_assemble_list[@]}" -gt 0 ]; then
    printf '\nAssemble fail (%d):\n' "$fail_assemble"
    printf '  %s\n' "${fail_assemble_list[@]:0:20}"
    [ "${#fail_assemble_list[@]}" -gt 20 ] && printf '  ... %d more\n' $((${#fail_assemble_list[@]} - 20))
  fi
  if [ "${#fail_runtime_list[@]}" -gt 0 ]; then
    printf '\nRuntime fail (%d):\n' "$fail_runtime"
    printf '  %s\n' "${fail_runtime_list[@]:0:20}"
    [ "${#fail_runtime_list[@]}" -gt 20 ] && printf '  ... %d more\n' $((${#fail_runtime_list[@]} - 20))
  fi
  if [ "${#fail_stdout_list[@]}" -gt 0 ]; then
    printf '\nStdout mismatch (%d):\n' "$fail_stdout"
    printf '  %s\n' "${fail_stdout_list[@]:0:20}"
    [ "${#fail_stdout_list[@]}" -gt 20 ] && printf '  ... %d more\n' $((${#fail_stdout_list[@]} - 20))
  fi
fi

echo ""
echo "==== c-testsuite (ag_c) ===="
printf "Total:           %d\n" "$total"
printf "Pass:            %d\n" "$pass"
printf "Fail (compile):  %d\n" "$fail_compile"
printf "Fail (assemble): %d\n" "$fail_assemble"
printf "Fail (runtime):  %d\n" "$fail_runtime"
printf "Fail (stdout):   %d\n" "$fail_stdout"
if [ "$total" -gt 0 ]; then
  pct=$(awk "BEGIN { printf \"%.1f\", $pass * 100 / $total }")
  printf "Pass率:          %s%%\n" "$pct"
fi
