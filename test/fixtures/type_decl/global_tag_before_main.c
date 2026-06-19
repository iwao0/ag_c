// グローバルスコープの struct タグ
// 期待: exit=7
#include <assert.h>
struct S { int x; };
int main(void) { assert(7 == 7); return 0; }
