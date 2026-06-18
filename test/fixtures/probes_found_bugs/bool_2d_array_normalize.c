// 2D _Bool 配列要素代入で 0/1 正規化
// 直前の修正は 1D _Bool 配列限定で、`_Bool m[2][3]` の要素代入 (m[1][0] = 99)
// は依然として正規化されなかった。1 段目の subscript は内側配列を返すため、
// build_subscript_deref がそこで pointee_is_bool を引き継がず、次の subscript
// の deref に is_bool が立たない。
// 「中間配列」と「最終要素」を inner_ds で判別して伝播するように修正。
#include <assert.h>
int main(void) {
  _Bool m[2][3] = {{1, 0, 1}, {0, 1, 0}};
  m[1][0] = 99;              // 1 に正規化されるべき
  assert(m[0][0] == 1);
  assert(m[0][1] == 0);
  assert(m[0][2] == 1);
  assert(m[1][0] == 1);   // 99 が 0/1 に正規化される (本バグの核心)
  assert(m[1][1] == 1);
  assert(m[1][2] == 0);
  return 0;
}
// 期待: 4
