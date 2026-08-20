/* C11 6.7.2.1: struct/union のメンバ位置に static_assert-declaration を書ける。
 *   struct S { _Static_assert(1+1 == 2, "math"); int x; };
 *
 * 以前は struct メンバ解析の冒頭で _Static_assert トークンが「メンバ型ではない」と
 * 判定され E3064 になっていた。後にfile-scope経路は対応したが、local recordの
 * direct typed-HIR経路では無診断拒否がE0006へ変換されていた。
 *
 * 修正: aggregate member parserでstatic assertionをmember宣言と分離し、file/localの
 * 両宣言経路で同じ定数式評価と診断を適用する。unionメンバ位置も同じ経路。 */
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
  typedef int LocalAssertType;
  enum { LOCAL_ASSERT_VALUE = 1 };
  struct LocalAssertRecord {
    _Static_assert(LOCAL_ASSERT_VALUE, "local enum");
    _Static_assert(sizeof(LocalAssertType) == sizeof(int), "local type");
    int value;
  } local = { 30 };
  union LocalAssertUnion {
    _Static_assert(LOCAL_ASSERT_VALUE, "local union");
    int value;
    long long wide;
  } local_union;
  struct LocalAssertOuter {
    struct LocalAssertInner {
      _Static_assert(LOCAL_ASSERT_VALUE, "nested local record");
      int value;
    } inner;
    int tail;
  } local_outer = { { 50 }, 60 };

  struct S1 s = { 10, 20 };
  assert(s.x == 10 && s.y == 20);

  union U u;
  u.n = 42;
  assert(u.n == 42);

  struct Outer o = { { 7 }, 8 };
  assert(o.inner.a == 7 && o.b == 8);

  local_union.value = 40;
  assert(local.value == 30 && local_union.value == 40);
  assert(local_outer.inner.value == 50 && local_outer.tail == 60);

  return 0;
}
