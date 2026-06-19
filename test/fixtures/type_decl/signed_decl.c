// signed (= int) 型で負の初期化
// 期待: exit=1 (-3+4)
#include <assert.h>
int main(void) { signed s = -3; assert(s + 4 == 1); return 0; }
