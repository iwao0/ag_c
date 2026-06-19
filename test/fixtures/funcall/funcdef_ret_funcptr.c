// 関数ポインタを返す関数の宣言 (パース確認)
// 期待: exit=0
#include <assert.h>
int (*f(void))(int) { return 0; }
int main(void) { assert(f() == 0); return 0; }
