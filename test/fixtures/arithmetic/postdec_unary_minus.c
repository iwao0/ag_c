// 後置デクリメントと単項 - を併用
// x-- 評価値 5、 - -3 → 5+3=8
// 期待: exit=8
#include <assert.h>
int main(void) { int x=5; assert(x-- - -3 == 8); return 0; }
