// volatile int 変数
// 期待: exit=9
#include <assert.h>
int main(void) { volatile int x = 9; assert(x == 9); return 0; }
