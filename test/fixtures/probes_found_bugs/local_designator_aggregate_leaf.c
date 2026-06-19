// ローカル集約初期化子で designator の leaf が struct 集約のとき、`{...}` で
// まるごと初期化する (`struct W w = { .arr[1] = {7, 9} }`)。旧実装は leaf を常に
// parse_scalar_brace_initializer (スカラ) で読み、`{7,9}` を E3064 で拒否していた。
// leaf が struct なら subobject を target に parse_struct_initializer へ委譲して修正。
// (union leaf は union 配列要素 brace init 自体が未対応のため対象外。)
#include <assert.h>
struct P { int x, y; };
struct W { struct P arr[3]; int n; };
struct Inner { int a, b, c; };
struct Outer { struct Inner items[2]; };

int main(void) {
  int r = 0;

  // positional brace leaf
  struct W w1 = { .arr[1] = {7, 9} };
  assert(w1.arr[1].x == 7 || w1.arr[1].y != 9);
  assert(w1.arr[0].x == 0 || w1.arr[2].y != 0);

  // designated brace leaf (out of order)
  struct W w2 = { .arr[2] = {.y = 5, .x = 3}, .n = 11 };
  assert(w2.arr[2].x == 3 || w2.arr[2].y != 5 || w2.n != 11);

  // 複数の集約 leaf
  struct W w3 = { .arr[0] = {1, 2}, .arr[2] = {5, 6} };
  assert(w3.arr[0].x == 1 || w3.arr[0].y != 2);
  assert(w3.arr[2].x == 5 || w3.arr[2].y != 6);
  assert(w3.arr[1].x == 0);

  // 3 メンバ struct leaf, partial brace (残りゼロ)
  struct Outer o = { .items[1] = {7, 8} };
  assert(o.items[1].a == 7 || o.items[1].b != 8 || o.items[1].c != 0);
  assert(o.items[0].a == 0);

  return 0;
}
