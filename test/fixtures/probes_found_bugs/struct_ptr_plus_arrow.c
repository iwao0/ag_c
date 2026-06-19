// struct ポインタ算術
#include <assert.h>
struct P { int x; int y; };
int main(void) {
  struct P arr[3] = { {1,2}, {3,4}, {5,6} };
  struct P *p = arr;
  assert((p+1)->x == 3); assert((p+2)->y == 6); return 0;  // 3 + 6 = 9
}
// 期待: 9
