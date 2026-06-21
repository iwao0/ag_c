/* union 配列要素の brace 初期化 (C11 6.7.9)。`union U a[2] = {[1]={.n=5}}` は
 * コンパイルは通るが値が格納されず a[1].n が 0 に化けていた。parse_array_elem_struct_brace_init
 * が配列要素を常に parse_struct_initializer へ投げており、union 要素の `.n=5` を
 * struct レイアウトで誤解決して代入を出していなかった。要素が union のときは
 * parse_union_initializer へ委譲して直す。 */
#include <assert.h>

union U { int n; long l; };
union V { int i; double d; char c[8]; };

int main(void){
  /* designator 要素 */
  union U a[2] = {[1] = {.n = 5}};
  assert(a[0].n == 0 && a[1].n == 5);

  /* positional 要素 */
  union U b[3] = {{.n = 10}, {.n = 20}, {.n = 30}};
  assert(b[0].n == 10 && b[1].n == 20 && b[2].n == 30);

  /* 要素ごとに別メンバを初期化 */
  union U c[2] = {[0] = {.n = 1}, [1] = {.l = 0x1122334455L}};
  assert(c[0].n == 1 && c[1].l == 0x1122334455L);

  /* 部分初期化: 残り要素は 0 */
  union U d[4] = {[2] = {.n = 99}};
  assert(d[0].n == 0 && d[1].n == 0 && d[2].n == 99 && d[3].n == 0);

  /* 異なるメンバ型 (double / char 配列) */
  union V e[2] = {{.d = 2.5}, {.i = 7}};
  assert(e[0].d == 2.5);
  assert(e[1].i == 7);

  /* designator と positional の混在 */
  union U f[3] = {{.n = 1}, [2] = {.n = 3}};
  assert(f[0].n == 1 && f[1].n == 0 && f[2].n == 3);
  return 0;
}
