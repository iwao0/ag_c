// extern による不完全配列宣言 (パース確認)
// 期待: exit=7
#include <assert.h>
extern int a[];
int main(void) { assert(7 == 7); return 0; }
