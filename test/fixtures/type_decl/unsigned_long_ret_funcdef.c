// unsigned long 戻り値関数
// 期待: exit=42
#include <assert.h>
unsigned long foo(int x) { return (unsigned long)x; }
int main(void) { assert((int)foo(42) == 42); return 0; }
