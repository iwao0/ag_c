// 三項のネスト (条件位置)
// 1 ? (2 ? 3 : 4) : 5 = 3
// 期待: exit=3
#include <assert.h>
int main(void) { assert((1 ? 2 ? 3 : 4 : 5) == 3); return 0; }
