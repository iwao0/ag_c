#!/bin/zsh
# ag_c と clang の実行結果を比較する差分テストヘルパ。
# 使い方: scripts/agc_diff_test.sh <src.c>   (src は絶対パス推奨。ROOT へ cd するため)
#
#   ag_c: src -> .s (stdout) -> clang で assemble+link -> 実行
#   参照: clang -w -I include で直接 build -> 実行
#   同梱ヘッダ (include/stdarg.h 等) を使う #include は ROOT へ cd して解決する。
#
# 【exit code だけ比較する罠を回避】
#   assert 失敗はすべて exit 134 に潰れるため、exit code だけ比較すると ag_c と clang が
#   「別々の assert で abort」しても両方 134 で OK と誤表示する。これを避けるため
#   exit code・stdout・正規化した stderr の 3 つを比較する。stderr は assert メッセージの
#   `file <path>,` 部分のみ正規化 (ag_c=basename / clang=フルパスの差を吸収) し、残りの
#   式・関数・行番号を比較する。挙動が同じなら 3 つすべて一致するはず。printf 系の stdout 差も
#   同じツールで検出できる。
set -u
SRC="$1"
# repo ルートはこのスクリプト (scripts/agc_diff_test.sh) の親ディレクトリ。
ROOT="${0:A:h:h}"
AGC="$ROOT/build/ag_c"
T="/tmp/_agc_$$"
S="$T.s"
BIN_AGC="$T.agc.bin"; BIN_REF="$T.ref.bin"
AGC_OUT="$T.agc.out"; AGC_ERR="$T.agc.err"
REF_OUT="$T.ref.out"; REF_ERR="$T.ref.err"
CERR="$T.cerr"
cleanup() { rm -f "$S" "$BIN_AGC" "$BIN_REF" "$AGC_OUT" "$AGC_ERR" "$REF_OUT" "$REF_ERR" "$CERR"; }
trap cleanup EXIT

cd "$ROOT" || exit 99

# ag_c でコンパイル (stdout に asm)
if ! "$AGC" "$SRC" > "$S" 2>"$CERR"; then
  echo "AGC_COMPILE_FAIL"; head -3 "$CERR"; exit 1
fi
if ! clang -o "$BIN_AGC" "$S" 2>"$CERR"; then
  echo "AGC_ASSEMBLE_FAIL"; head -5 "$CERR"; exit 1
fi

# 参照 clang (bundled ヘッダ用に -I include、無ければ -I 無しで再試行)
if ! clang -w -I "$ROOT/include" -o "$BIN_REF" "$SRC" 2>/dev/null; then
  if ! clang -w -o "$BIN_REF" "$SRC" 2>/dev/null; then
    echo "REF_COMPILE_FAIL"; exit 1
  fi
fi

# 実行 (stdout / stderr / exit code を個別に捕捉)
"$BIN_AGC" >"$AGC_OUT" 2>"$AGC_ERR"; AGC_RC=$?
"$BIN_REF" >"$REF_OUT" 2>"$REF_ERR"; REF_RC=$?

# assert メッセージの file パス差を正規化 (ag_c=basename / clang=フルパス)
norm() { sed -E 's@file [^,]+,@file X,@g' "$1"; }

DIFF=""
[ "$AGC_RC" -ne "$REF_RC" ] && DIFF="$DIFF rc(agc=$AGC_RC ref=$REF_RC)"
cmp -s "$AGC_OUT" "$REF_OUT" || DIFF="$DIFF stdout"
if ! diff -q <(norm "$AGC_ERR") <(norm "$REF_ERR") >/dev/null; then
  DIFF="$DIFF stderr"
fi

if [ -z "$DIFF" ]; then
  echo "OK  agc=$AGC_RC ref=$REF_RC"
else
  echo "MISMATCH [$DIFF]  agc=$AGC_RC ref=$REF_RC  <<<<<<<<<<"
  case "$DIFF" in
    *stdout*) echo "--- stdout (agc | ref) ---"; diff <(cat "$AGC_OUT") <(cat "$REF_OUT") | head -10 ;;
  esac
  case "$DIFF" in
    *stderr*) echo "--- stderr (agc | ref, file 正規化済) ---"; diff <(norm "$AGC_ERR") <(norm "$REF_ERR") | head -10 ;;
  esac
fi
