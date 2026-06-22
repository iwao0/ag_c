/* 「関数ポインタ配列へのポインタ」(`int (*(*pa)[N])(args)` / `BinOp (*pa)[N]`) の宣言:
 *   typedef int (*BinOp)(int, int);
 *   BinOp ops[3] = {add, sub, mul};
 *   BinOp (*pa)[3] = &ops;
 *   (*pa)[0](7, 2);  /* 旧版は SIGSEGV (4B ldrsw で関数ポインタを読む) *\/
 *
 * 以前は decl.c の `(*p)[N]` 経路 (paren_array_mul>0) が要素サイズに常に elem_size を
 * 使い、関数ポインタ要素 (8B) を 4B (戻り型 int のサイズ) として登録していた。結果
 * `(*pa)[i]` の deref が `ldrsw` (4B 符号拡張ロード) で出力され、関数ポインタを下位 4B
 * だけ読んで bl したため SIGSEGV になっていた。
 *
 * 修正: 同経路で要素サイズを 8 (ポインタサイズ) に上書きする条件を 2 つ追加:
 *   (a) 宣言子の trailing 部に関数シグネチャ `(args...)` がある (g_decl_trailing_func_suffix)
 *       → `int (*(*pa)[3])(int,int)` 形式
 *   (b) 基底型 typedef がポインタ型 (base_is_pointer)
 *       → `BinOp (*pa)[3]` / `typedef int *IP; IP (*pa)[3]` 形式
 *
 * 限界 (未対応、次セッション課題):
 * (a) `typedef BinOp OpArr3[3]; OpArr3 *pa` (paren なしで typedef 配列ポインタ) は別経路
 *     (decl.c の is_pointer + td_array_dim_count>0 分岐) で、`PA p = m` (= `typedef int (*PA)[3]`) と
 *     同じ経路を共有しており、単純な base_is_pointer 判定では PA p のケースを壊す。
 *     別途要素型を typedef_info から精密に判定する仕組みが要る。
 * (b) `typedef int *IP; IP (*pia)[3]` 形 (paren 経路でデータポインタ配列) の `*(*pia)[0]` の
 *     最終 deref サイズ伝播。本修正で paren 経路の `var->base_deref_size = 8` は設定するが、
 *     subscript 結果 `(*pia)[0]` を更に `*` で deref するときの sizing 連鎖がデータポインタ
 *     要素について int (4B) load にならない (関数ポインタ要素 (a)(b) は呼び出し時に正しく
 *     bl になるため OK)。 */
#include <assert.h>

typedef int (*BinOp)(int, int);

static int my_add(int a, int b) { return a + b; }
static int my_sub(int a, int b) { return a - b; }
static int my_mul(int a, int b) { return a * b; }

BinOp ops[3] = { my_add, my_sub, my_mul };

int main(void) {
  /* (1) typedef + 関数ポインタ配列へのポインタ */
  BinOp (*pa)[3] = &ops;
  assert((*pa)[0](7, 2) == 9);
  assert((*pa)[1](7, 2) == 5);
  assert((*pa)[2](7, 2) == 14);

  /* (2) 直接表記 (typedef なし) */
  int (*(*pb)[3])(int, int) = &ops;
  assert((*pb)[0](10, 3) == 13);
  assert((*pb)[1](10, 3) == 7);
  assert((*pb)[2](10, 3) == 30);

  return 0;
}
