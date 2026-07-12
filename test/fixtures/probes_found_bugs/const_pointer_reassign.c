// `const int *p` (pointee const) のポインタ自身は再代入可能
// 修正前: ps_node_reject_const_assign が is_const_qualified を見ていたが、
// ag_c では「ポインタ変数の is_const_qualified = pointee の const」という慣習
// (_Generic 等で使用) のため、`const int *p = &x; p = &y;` で `p` 自体への
// 再代入を誤って拒否していた (E3077).
//
// ポインタ変数の場合は pointer_const_qual_mask の bit 0
// (= ポインタ自身の const、`int * const p` のケース) を見るように修正。
#include <assert.h>
int main(void) {
  int x = 7;
  int y = 13;
  const int *p = &x;
  p = &y;
  assert(*p == 13); return 0; // 13
}
// 期待: 13
