// _Bool conversion
#include <assert.h>
int main(void) {
  _Bool b1 = 42;   // → 1
  _Bool b2 = 0;    // → 0
  _Bool b3 = -1;   // → 1
  assert(b1 == 1);   // 42 -> 1
  assert(b2 == 0);   // 0  -> 0
  assert(b3 == 1);   // -1 -> 1
  return 0;
}
// 期待: 2
