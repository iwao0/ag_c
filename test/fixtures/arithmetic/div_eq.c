// 複合代入 /=
// 期待: exit=4
#include <assert.h>
int main(void) { int a = 8; a /= 2; assert(a == 4); return 0; }
