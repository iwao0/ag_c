// 前置インクリメント + 加算
// ++x で 3、 +y=3 → 6
// 期待: exit=6
#include <assert.h>
int main(void) { int x=2; int y=3; assert(++x+y == 6); return 0; }
