// 関数ポインタ配列を含む struct
#include <assert.h>
int op_add(int a, int b) { return a + b; }
int op_sub(int a, int b) { return a - b; }
struct Ops { int (*fns[2])(int, int); };
int main(void) {
  struct Ops ops;
  ops.fns[0] = op_add;
  ops.fns[1] = op_sub;
  assert(ops.fns[0](10, 5) == 15); assert(ops.fns[1](20, 3) == 17); return 0;  // 15 + 17 = 32
}
// 期待: 32
