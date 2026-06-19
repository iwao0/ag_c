// メンバへのポインタ
#include <assert.h>
struct S { int v[3]; };
int main(void) {
  struct S s = { {10, 20, 30} };
  int *p = s.v;
  assert(p[0] == 10); assert(p[1] == 20); assert(p[2] == 30); return 0;
}
// 期待: 60
