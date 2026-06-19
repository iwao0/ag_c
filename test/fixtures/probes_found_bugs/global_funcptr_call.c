// グローバル関数ポインタ
#include <assert.h>
int add(int a, int b) { return a + b; }
int (*gp)(int, int) = add;
int main(void) {
  assert(gp(20, 22) == 42); return 0;
}
// 期待: 42
