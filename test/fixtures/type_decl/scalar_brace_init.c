// スカラ変数の波括弧初期化 (C99)
// 期待: exit=3
#include <assert.h>
int main(void) { int x = {3}; assert(x == 3); return 0; }
