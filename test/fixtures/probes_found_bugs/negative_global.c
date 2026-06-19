// グローバル変数の負数初期化
#include <assert.h>
int g = -42;
int *gp = (int *)0;
int main(void) {
  assert(g == -42); assert(gp == 0); return 0;
}
// 期待: 42
