// グローバル配列の sparse designator
#include <assert.h>
int g[10] = { [0]=1, [3]=8, [9]=21 };
int main(void) {
  assert(g[0] == 1); assert(g[3] == 8); assert(g[5] == 0); assert(g[9] == 21); return 0;
}
// 期待: 1 + 8 + 0 + 21 = 30
