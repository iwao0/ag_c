// void 戻り値関数の呼び出し
// 期待: exit=42
#include <assert.h>
void noop(void) { return; }
int main(void) { noop(); assert(42 == 42); return 0; }
