// `const int *p` (pointee const) のポインタ自身は再代入可能
// 修正前はポインタ自身と pointee の const を区別できず、
// `const int *p = &x; p = &y;` で p 自体への再代入を誤って拒否していた (E3077).
//
// 現在は pointer node 自身の qualifier (`int * const p`) と base node の
// qualifier (`const int *p`) を再帰型上で分けて判定する。
#include <assert.h>
int main(void) {
  int x = 7;
  int y = 13;
  const int *p = &x;
  p = &y;
  int *const q = &x;
  int *const r = &y;
  int *const *pp = &q;
  pp = &r;
  assert(*p == 13);
  assert(**pp == 13); return 0; // 13
}
// 期待: 13
