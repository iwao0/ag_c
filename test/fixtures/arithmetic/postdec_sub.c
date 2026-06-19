// `x---y` は `x-- - y` と読まれる
// x-- 評価値 3、 -y=2 → 3-2=1
// 期待: exit=1
#include <assert.h>
int main(void) { int x=3; int y=2; assert(x---y == 1); return 0; }
