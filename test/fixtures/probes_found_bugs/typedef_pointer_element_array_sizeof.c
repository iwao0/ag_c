/* `typedef T A[N]` で base が pointer typedef のときの sizeof / typedef_info:
 *   typedef int (*BinOp)(int, int);
 *   typedef BinOp OpArr3[3];
 *   sizeof(OpArr3);  /* 旧: 8 (= BinOp サイズのまま、配列倍されない) → 正: 24 *\/
 *
 * 以前は compute_toplevel_typedef_sizeof / define_local_typedef_from_declarator が
 * `is_ptr ? 8 : elem_size` で要素サイズを決め、is_ptr=1 のとき配列 suffix `[N]` を
 * 反映していなかった。結果 typedef_info.sizeof_size = 8 (BinOp サイズ) のままで
 * is_array=0 として登録され、OpArr3 *pa の宣言や sizeof(OpArr3) が誤値になっていた。
 *
 * 修正: pointer-element 配列 typedef (base が pointer typedef + declarator に `*` 追加なし +
 * 配列 suffix あり) の場合に sizeof_size = 8 * arr_total、is_array=1 として登録する。
 * pointer-to-array typedef (`typedef int (*PA)[3]`、`*` が括弧内 = ptr_in_paren_group) とは
 * 排他なので、`PA p; sizeof(p)` 等の従来挙動には影響しない。
 *
 * sizeof 評価側 (expr.c) も `(!td_ptr || td_is_array)` で sizeof_size を読むよう拡張し、
 * is_pointer=1 でも is_array=1 ならネスト要素サイズが反映されるようにした。
 *
 * 限界 (未対応、次セッション課題): OpArr3 *pa の実体宣言と `(*pa)[i](...)` 呼び出しは
 * call 経路の deref サイズ伝播がまだ不完全で SIGBUS する。本修正は sizeof と
 * typedef_info の登録のみで、call まで届かない。 */
#include <assert.h>

typedef int (*BinOp)(int, int);
typedef BinOp OpArr3[3];

/* 配列要素が単段データポインタの場合 */
typedef int *IP;
typedef IP IPA4[4];

/* 配列要素が単段 char ポインタ (文字列ポインタ) */
typedef const char *StrPtr;
typedef StrPtr StrArr5[5];

int main(void) {
  /* (1) 関数ポインタ要素 */
  assert(sizeof(OpArr3) == 3 * sizeof(BinOp));
  assert(sizeof(OpArr3) == 3 * 8);

  /* (2) データポインタ要素 */
  assert(sizeof(IPA4) == 4 * sizeof(IP));
  assert(sizeof(IPA4) == 4 * 8);

  /* (3) const char* 要素 */
  assert(sizeof(StrArr5) == 5 * sizeof(StrPtr));
  assert(sizeof(StrArr5) == 5 * 8);

  /* (4) 回帰確認: 単純 pointer typedef は 8 のまま */
  assert(sizeof(BinOp) == 8);
  assert(sizeof(IP) == 8);
  assert(sizeof(StrPtr) == 8);

  /* (5) 回帰確認: pointer-to-array typedef は 8 (pointer) */
  typedef int (*PA)[3];
  assert(sizeof(PA) == 8);

  /* (6) 回帰確認: 普通の配列 typedef */
  typedef int IntArr3[3];
  assert(sizeof(IntArr3) == 12);

  return 0;
}
