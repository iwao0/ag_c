// ポインタの基本: 取得 (& と *)
// 期待: exit=5
#include <assert.h>
int main(void) { int x = 5; int *p = &x; assert(*p == 5); return 0; }
