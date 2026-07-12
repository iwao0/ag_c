/* タグ戻り型 + `(*...)` を含む複雑な宣言子の関数。`struct P (*f())[3]` (配列へのポインタ戻り) /
 * `struct R (*f())(int)` (関数ポインタ戻り) (C11 6.7.6)。旧トップレベル先読みが
 * `(*...)` 宣言子を扱えず、変数宣言と誤判定して E2006 になっていた。
 * 現在は宣言子を一度だけ構文解析し、直後の`{`で関数定義を分類する。 */
#include <assert.h>

struct P { int x, y; };
static struct P pts[3] = {{1, 2}, {3, 4}, {5, 6}};

/* タグ + 配列へのポインタ戻り */
struct P (*get_pts(void))[3] { return &pts; }

/* タグ + 関数ポインタ戻り */
struct R { int v; };
static struct R r_inc(int a) { struct R r = {a + 1}; return r; }
struct R (*get_op(void))(int) { return r_inc; }

int main(void) {
  /* (*f())[i].member と型付き変数経由 */
  assert((*get_pts())[1].x == 3 && (*get_pts())[2].y == 6);
  struct P (*p)[3] = get_pts();
  assert(p[0][0].x == 1 && p[0][2].y == 6);

  /* タグ戻りの関数ポインタを呼ぶ (struct 戻りは一旦変数で受ける) */
  struct R (*op)(int) = get_op();
  struct R r1 = op(41);
  assert(r1.v == 42);
  struct R r2 = get_op()(9);
  assert(r2.v == 10);
  return 0;
}
