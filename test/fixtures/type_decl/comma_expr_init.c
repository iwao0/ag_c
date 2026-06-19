// カンマ式の初期化子 (1,2,3,4,5) → 5
// 期待: exit=5
#include <assert.h>
int main(void) { int x = (1, 2, 3, 4, 5); assert(x == 5); return 0; }
