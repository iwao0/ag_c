/* `static struct S *f(...) {...}` 等、storage class (static/extern) を伴うタグ戻り型の
 * 関数定義 (C11 6.7.1 / 6.9.1)。is_toplevel_function_signature と parse_func_decl_spec が
 * どちらも「タグキーワードの前にある storage class」を飛ばしておらず、`static struct S *g()`
 * がオブジェクト宣言と誤判定 (E2006 `;` 期待) / 戻り型が implicit int に化けて E3064 に
 * なっていた。`static int *g()` (builtin) や非 static の `struct S *g()` は動いていた。
 * 修正: 両関数で storage class の直後がタグキーワードなら storage class を消費してから
 * タグ経路へ入る。pointer/値返し、struct/union/enum、引数あり/なしを網羅。 */
#include <assert.h>

struct P { int x, y; };
union  U { int a; long b; };
enum   E { E0, E1, E9 = 9 };

static struct P sp = {3, 4};
static union  U su = {.b = 0x1122334455L};
static enum   E se = E9;

/* static + タグ + ポインタ戻り (本バグの中心) */
static struct P *get_p(void) { return &sp; }
static union  U *get_u(void) { return &su; }
static enum   E *get_e(void) { return &se; }
/* 引数あり */
static struct P *get_p_arg(int d) { sp.x += d; return &sp; }
/* static + タグ + 値返し */
static struct P make_p(int a, int b) { struct P p = {a, b}; return p; }
/* 戻り値を subscript (前コミットの func-pointer-return と組み合わせ) */
static struct P arr[3] = {{1, 2}, {3, 4}, {5, 6}};
static struct P *get_arr(void) { return arr; }

int main(void) {
  assert(get_p()->x == 3 && get_p()->y == 4);
  assert(get_u()->b == 0x1122334455L);
  assert(*get_e() == E9);
  assert(get_p_arg(10)->x == 13);     /* sp.x: 3 -> 13 */
  struct P m = make_p(7, 8);
  assert(m.x == 7 && m.y == 8);
  assert(get_arr()[2].x == 5 && get_arr()[1].y == 4);
  return 0;
}
