// 複合代入 %=
// 期待: exit=2
#include <assert.h>
int main(void) { int a = 10; a %= 4; assert(a == 2); return 0; }
