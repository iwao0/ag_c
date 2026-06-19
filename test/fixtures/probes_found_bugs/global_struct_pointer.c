// グローバル構造体ポインタ
#include <assert.h>
struct P { int x; };
struct P gp = {42};
struct P *pp = &gp;
int main(void) {
  assert(pp->x == 42); return 0;
}
// 期待: 42
