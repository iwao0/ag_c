/* `*` の後にポインタ修飾子が付く戻り型 `int *const f()` / `int *volatile f()`
 * (const/volatile-qualified pointer return) (C11 6.7.6.1)。旧トップレベル先読みと
 * pointer suffix解析が `*` の後の const/volatile を飛ばさず、関数と
 * 認識されずオブジェクト宣言と誤判定して E2006 (`;` 期待、実際 '{') になっていた。
 * `const int *f()` (pointer to const) は動作。lookahead と suffix 解析の両方で `*` の後の
 * const/volatile を読み飛ばす。現在の分類は構文解析済み宣言子を正本にする。 */
#include <assert.h>

static int x = 42;
static int y = 7;
static double d = 1.5;

int *const fc(void) { return &x; }              /* const ポインタ戻り */
int *volatile fv(void) { return &y; }           /* volatile ポインタ戻り */
double *const fd(void) { return &d; }           /* fp const ポインタ戻り */
struct S { int v; };
static struct S s = {5};
struct S *const fs(void) { return &s; }         /* タグ戻り + const ポインタ */

int main(void) {
  assert(*fc() == 42);
  *fc() = 100;
  assert(x == 100);
  x = 42;
  assert(*fv() == 7);
  assert(*fd() == 1.5);
  assert(fc()[0] == 42);                          /* subscript も動く (前コミットと併用) */
  assert(fs()->v == 5);
  return 0;
}
