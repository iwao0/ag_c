// 複合代入 <<=
// 期待: exit=12
#include <assert.h>
int main(void) { int a = 3; a <<= 2; assert(a == 12); return 0; }
