// 後置インクリメント・デクリメント混在
// x++ 評価値 10 → x=11、 y-- 評価値 20 → y=19
// 10 - 20 + 11 = 1
// 期待: exit=1
#include <assert.h>
int main(void) { int x=10; int y=20; assert(x++-y--+x == 1); return 0; }
