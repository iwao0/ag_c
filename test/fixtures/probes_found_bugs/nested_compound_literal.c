// ネストした compound literal
#include <assert.h>
struct Inner { int v; };
struct Outer { struct Inner inner; int extra; };
int main(void) {
  struct Outer o = (struct Outer){ (struct Inner){42}, 8 };
  assert(o.inner.v == 42); assert(o.extra == 8); return 0;
}
// 期待: 50
