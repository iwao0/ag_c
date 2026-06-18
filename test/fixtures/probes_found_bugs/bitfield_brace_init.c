// bitfield メンバを持つ struct の brace 初期化
// 修正前: new_struct_member_lvar が bit_width/offset を lvar ノードに載せず、
// IR builder が「storage 全体を上書き」する非 bitfield ストアを発行していた。
// その結果、`struct S s = {2, 3};` で a=2 を書いた直後の b=3 書き込みが
// a 含む全 8bit を 3 で上書き → 後段の compound assign も古い値が読めず崩壊。
#include <assert.h>
struct S { unsigned a:3; unsigned b:5; };
int main(void) {
  struct S s = {2, 3};
  s.a += 5;       // 2+5=7
  s.b |= 0x10;    // 3|16=19
  assert(s.a == 7);    // bitfield a が b と独立に保持されること
  assert(s.b == 19);
  return 0;
}
// 期待: 89
