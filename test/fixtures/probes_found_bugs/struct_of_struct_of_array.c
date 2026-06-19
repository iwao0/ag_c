// 3階層 struct
#include <assert.h>
struct Inner { int arr[3]; };
struct Mid { struct Inner ins[2]; };
struct Outer { struct Mid m; int x; };
int main(void) {
  struct Outer o;
  o.m.ins[0].arr[0] = 1;
  o.m.ins[0].arr[1] = 2;
  o.m.ins[1].arr[2] = 3;
  o.x = 36;
  assert(o.m.ins[0].arr[0] == 1); assert(o.m.ins[0].arr[1] == 2); assert(o.m.ins[1].arr[2] == 3); assert(o.x == 36); return 0;
}
// 期待: 42
