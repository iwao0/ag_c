/* C11 6.7.2.1: struct/union のメンバ位置に static_assert-declaration を書ける。
 *   struct S { _Static_assert(1+1 == 2, "math"); int x; };
 *
 * 以前は struct メンバ解析の冒頭で _Static_assert トークンが「メンバ型ではない」と
 * 判定され E3064 になっていた。ローカル経路 / トップレベル経路では既に対応済み。
 *
 * 修正: struct_layout.c のメンバ解析ループ冒頭に TK_STATIC_ASSERT 分岐を追加。
 * `_Static_assert(expr, "msg");` の expr を psx_decl_eval_const_int で畳み込み、
 * 偽なら診断、真なら continue で次のメンバへ。union メンバ位置も同じ経路。 */
#include <assert.h>

struct S1 {
  _Static_assert(sizeof(int) == 4, "int must be 4");
  int x;
  _Static_assert(1 + 1 == 2, "math is broken");
  int y;
};

union U {
  _Static_assert(sizeof(double) == 8, "double must be 8");
  int n;
  double d;
};

/* ネスト struct でも動く */
struct Outer {
  _Static_assert(2 * 3 == 6, "x");
  struct Inner {
    _Static_assert(4 + 4 == 8, "y");
    int a;
  } inner;
  int b;
};

int main(void) {
  struct S1 s = { 10, 20 };
  assert(s.x == 10 && s.y == 20);

  union U u;
  u.n = 42;
  assert(u.n == 42);

  struct Outer o = { { 7 }, 8 };
  assert(o.inner.a == 7 && o.b == 8);

  return 0;
}
