// double メンバを持つ struct
#include <assert.h>
struct V { double x; double y; };
int main(void) {
  struct V v = {1.5, 2.5};
  assert((int)(v.x + v.y) == 4); return 0;  // 4
}
// 期待: 4
