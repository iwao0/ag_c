// 8 進数リテラル 0777 = 511 → 8 bit exit code mod 256 = 255
// 期待: exit=255 (true value: 511)
#include <assert.h>
int main(void) { assert(0777 == 511); return 0; }
