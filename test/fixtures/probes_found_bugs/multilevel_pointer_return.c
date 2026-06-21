/* 多段ポインタを返す関数の直接 deref `int **g(); **g()` が SIGSEGV だった。
 * semantic ctx の ret_is_pointer が bool で段数を持たず、`int **` を単段 `int *`
 * (pointee 4B) 扱い → `*g()` が int (4B) になり `**g()` が int 値をアドレスとして
 * 参照していた。型付き変数経由 `int **q=g(); **q` は元から動作。
 * 修正: 戻り型の `*` 段数を ret_pointer_levels に記録し、node_utils の funcall 経路
 *   (pointer_qual_levels / base_deref_size / ps_node_deref_size) が段数>=2 のとき
 *   `*g()` を「1 段減らしたポインタ」(8B 値, 最内基底型 deref) として組む。subscript
 *   `g()[i]` も genuine ポインタ値として 1 段消費する (build_subscript_deref)。 */
#include <assert.h>

int  v  = 42;     int *p  = &v;   int  **g(void)  { return &p;  }   /* int**   */
char cv = 90;     char *cp = &cv; char **cg(void) { return &cp; }   /* char**  */
int  w  = 7;      int *p1 = &w;   int **p2 = &p1;
int ***g3(void) { return &p2; }                                     /* int***  */
int  row[3] = {10, 20, 30};  int *rp = row;  int **rg(void) { return &rp; }

int main(void) {
  /* prefix deref */
  assert(**g() == 42);
  *p = 100;
  assert(**g() == 100);            /* 書き換えが見えること (実体を指している) */
  assert(**cg() == 90);            /* char** (最内 1B load) */
  assert(***g3() == 7);            /* 3 段 */

  /* deref + subscript 混在 */
  assert((*rg())[1] == 20);

  /* subscript 直接 `rg()[0][i]`: rg() は int**、rg()[0] は int*、rg()[0][i] は int */
  assert(rg()[0][0] == 10);
  assert(rg()[0][2] == 30);

  /* 型付き変数経由 (元から動作、回帰防止) */
  int **q = g();
  assert(**q == 100);
  return 0;
}
