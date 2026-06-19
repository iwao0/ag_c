// 複合代入 -=
// 期待: exit=2
#include <assert.h>
int main(void) { int a = 5; a -= 3; assert(a == 2); return 0; }
