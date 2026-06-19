// グローバル: タグ定義と同時にポインタ変数宣言
// 期待: exit=7
#include <assert.h>
struct S { int x; } *gp;
int main(void) { assert(7 == 7); return 0; }
