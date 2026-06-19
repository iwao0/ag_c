// パラメータ付きで関数ポインタを返す関数 (パース確認)
// 期待: exit=0
#include <assert.h>
int (*f(int n))(int) { return 0; }
int main(void) { assert(f(0) == 0); return 0; }
