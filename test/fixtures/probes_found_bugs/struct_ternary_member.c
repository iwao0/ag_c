// (cond ? a : b).x
#include <assert.h>
struct V { int v; };
int main(void) {
  struct V a = {10};
  struct V b = {20};
  int cond = 1;
  assert((cond ? a : b).v == 10); return 0;
}
// 期待: 10
