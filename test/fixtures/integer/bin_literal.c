// 2 進数リテラル 0b101010 = 42 (GNU 拡張、ag_c では既定で有効)
// 期待: exit=42
#include <assert.h>
int main(void) { assert(0b101010 == 42); return 0; }
