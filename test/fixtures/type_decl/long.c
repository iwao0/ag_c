// long 型の引数と戻り値
// 期待: exit=99 (98+1)
#include <assert.h>
long calc(long x) { return x + 1; }
int main(void) { assert(calc(98) == 99); return 0; }
