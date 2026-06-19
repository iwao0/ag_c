// 1 文で複数宣言 (一部初期化)
// 期待: exit=7
#include <assert.h>
int main(void) { int a, b = 7; assert(b == 7); return 0; }
