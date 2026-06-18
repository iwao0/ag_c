// 匿名 union (C11)
#include <assert.h>
struct N {
  int kind;
  union { int i; float f; };
};
int main(void) {
  struct N n;
  n.kind = 1; n.i = 42;
  assert(n.kind == 1);
  assert(n.i == 42);
  return 0;
}
// 期待: 52
