// 複合代入 |=
// 8 (1000) | 3 (0011) = 11 (1011)
// 期待: exit=11
#include <assert.h>
int main(void) { int a = 8; a |= 3; assert(a == 11); return 0; }
