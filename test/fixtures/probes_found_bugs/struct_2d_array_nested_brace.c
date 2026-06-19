// struct の 2D 配列を多重ネスト brace で初期化
// 修正前: parse_array_init_chunk の最内側ループは psx_expr_assign で
// 数値を読むのみで、struct 要素の `{1, 2}` 形式 (= ネスト brace) を受け
// られなかった (E3064: 数値が必要です ('{')).
//
// 多次元 struct/union 配列の最内側で curtok が '{' のとき、parse_array_
// elem_struct_brace_init に委譲するよう拡張。
#include <assert.h>
struct P { int x; int y; };
int main(void) {
  struct P grid[2][2] = {
    {{1,2},{3,4}},
    {{5,6},{7,8}}
  };
  assert(grid[0][0].x == 1); assert(grid[1][1].y == 8); return 0; // 1+8 = 9
}
// 期待: 9
