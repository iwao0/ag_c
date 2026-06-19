// カンマ演算子の連鎖
// (a=1, b=2, a+b) → 3
// 期待: exit=3
#include <assert.h>
int main(void) { int a = 0; int b = 0; assert((a = 1, b = 2, a + b) == 3); return 0; }
