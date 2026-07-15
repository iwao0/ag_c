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
 * 追加修正: OpArr3 *pa の実体宣言では typedef の配列要素が関数ポインタである分まで
 * pointer level に数え、配列 typedef へのポインタ stride 分岐から外れていた。
 * `(*pa)[i](...)` が配列アドレスでなく関数コードを int として読んで SIGBUS していたため、
 * 配列 typedef 自体に宣言子 `*` を足した形も stride 分岐に入れる。 */
#include <assert.h>

typedef int (*BinOp)(int, int);
typedef BinOp OpArr3[3];

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

/* 配列要素が単段データポインタの場合 */
typedef int *IP;
typedef IP IPA4[4];

/* 配列要素が単段 char ポインタ (文字列ポインタ) */
typedef const char *StrPtr;
typedef StrPtr StrArr5[5];

int main(void) {
  /* (1) 関数ポインタ要素 */
  assert(sizeof(OpArr3) == 3 * sizeof(BinOp));
  assert(sizeof(OpArr3) == 3 * sizeof(void*));

  /* (2) データポインタ要素 */
  assert(sizeof(IPA4) == 4 * sizeof(IP));
  assert(sizeof(IPA4) == 4 * sizeof(void*));

  /* (3) const char* 要素 */
  assert(sizeof(StrArr5) == 5 * sizeof(StrPtr));
  assert(sizeof(StrArr5) == 5 * sizeof(void*));

  /* (4) 回帰確認: 単純 pointer typedef は target pointer size */
  assert(sizeof(BinOp) == sizeof(void*));
  assert(sizeof(IP) == sizeof(void*));
  assert(sizeof(StrPtr) == sizeof(void*));

  /* (5) 回帰確認: pointer-to-array typedef は pointer */
  typedef int (*PA)[3];
  assert(sizeof(PA) == sizeof(void*));

  /* (6) 回帰確認: 普通の配列 typedef */
  typedef int IntArr3[3];
  assert(sizeof(IntArr3) == 12);

  /* (7) pointer to typedef array whose elements are function pointers */
  OpArr3 ops = {add, sub, mul};
  OpArr3 *pa = &ops;
  assert((*pa)[0](2, 3) == 5);
  assert((*pa)[1](9, 4) == 5);
  assert((*pa)[2](6, 7) == 42);

  return 0;
}
