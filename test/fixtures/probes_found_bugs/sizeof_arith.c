// sizeof(int + int) は 4
#include <assert.h>
int main(void) {
  int a = 1, b = 2;
  assert((int)sizeof(a + b) == 4); return 0;
}
// 期待: 4
