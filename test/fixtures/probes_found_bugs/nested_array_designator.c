// メンバパス途中に配列添字を含む designator (`.m.x[1].b`, `.arr[i].f`) が
// E2006 になっていた回帰テスト。consume_nested_designator_and_build_assign を
// `.member` と `[idx]` の任意連鎖を辿るよう一般化した。
#include <assert.h>
struct In { int a, b; };
struct Mid { struct In x[2]; };
struct Out { struct Mid m; int z; };

struct W { struct In arr[3]; int t; };

int main(void) {
  // メンバ -> 配列添字 -> メンバ の深いパス
  struct Out o = {.m.x[0].a = 1, .m.x[1].b = 7, .z = 3};
  int s1 = o.m.x[0].a * 100 + o.m.x[1].b * 10 + o.z; // 100+70+3 = 173

  // 配列メンバ直接添字 -> メンバ
  struct W w = {.arr[2].b = 5, .arr[0].a = 9, .t = 4};
  int s2 = w.arr[2].b * 10 + w.arr[0].a + w.t; // 50+9+4 = 63

  assert(s1 == 173); assert(s2 == 63); return 0; // 173-63-68 = 42
}
