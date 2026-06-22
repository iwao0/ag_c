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
 * 後続修正 (続き20〜21 で対応):
 *  - `typedef BinOp OpArr3[3]` の typedef_info を「is_array=1, sizeof_size=8*N」で登録するよう
 *    parser.c / decl.c / sizeof 経路を改修 (typedef_pointer_element_array_sizeof.c で網羅)。
 *  - データポインタ要素配列 (`IP (*pia)[3]`) の `*(*pia)[0]` 最終 deref を pointee サイズ
 *    (int=4) で出すよう、宣言経路で pointer_qual_levels=1 / base_deref_size=pointee を立てて
 *    build_unary_deref_node で carry する (本 fixture の IP ケースで網羅)。 */
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

  /* (3) typedef int *IP; IP (*pia)[3] (データポインタ配列へのポインタ): 続き21 修正で
   * `*(*pia)[0]` の最終 deref が pointee サイズ (int=4) で出力されるようになった。
   * 直接比較 `== 100` も型情報が int で渡り正しく動く。 */
  typedef int *IP;
  int x = 100, y = 200, z = 300;
  IP arr[3] = { &x, &y, &z };
  IP (*pia)[3] = &arr;
  assert(*(*pia)[0] == 100);
  assert(*(*pia)[1] == 200);
  assert(*(*pia)[2] == 300);
  int v = *(*pia)[0];   /* 代入経路も従来通り */
  assert(v == 100);

  return 0;
}
