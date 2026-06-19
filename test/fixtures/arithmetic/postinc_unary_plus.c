// 後置インクリメントと単項 + を併用
// x++ 評価値 1、 +1 → 1+1=2
// 期待: exit=2
#include <assert.h>
int main(void) { int x=1; assert(x++ + +1 == 2); return 0; }
