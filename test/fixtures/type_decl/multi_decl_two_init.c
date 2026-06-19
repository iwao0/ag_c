// 1 文で複数宣言 (両方初期化)
// 期待: exit=7
#include <assert.h>
int main(void) { int a = 3, b = 4; assert(a + b == 7); return 0; }
