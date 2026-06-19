// 複合代入 &=
// 14 (1110) & 3 (0011) = 2 (0010)
// 期待: exit=2
#include <assert.h>
int main(void) { int a = 14; a &= 3; assert(a == 2); return 0; }
