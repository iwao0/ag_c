// (short)(700*100) = 70000 → short ラップ = 4464 → exit code mod 256 = 112
// 期待: exit=112
#include <assert.h>
int main(void) { assert((short)(700 * 100) == 4464); return 0; }
