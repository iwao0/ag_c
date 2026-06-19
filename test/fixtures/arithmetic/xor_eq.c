// 複合代入 ^=
// 14 (1110) ^ 3 (0011) = 13 (1101)
// 期待: exit=13
#include <assert.h>
int main(void) { int a = 14; a ^= 3; assert(a == 13); return 0; }
