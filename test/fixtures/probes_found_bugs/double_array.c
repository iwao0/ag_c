// double 配列
#include <assert.h>
int main(void) {
  double a[3] = {1.5, 2.5, 3.5};
  assert((int)(a[0] + a[1] + a[2]) == 7);  // 7
  return 0;
}
// 期待: 7
