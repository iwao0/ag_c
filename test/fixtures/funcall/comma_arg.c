// 関数引数内のカンマ式 (1,2) → 2
// f(2, 3) = 2*10+3 = 23
// 期待: exit=23
#include <assert.h>
f(x, y) { return x*10 + y; }
main() { assert(f((1, 2), 3) == 23); return 0; }
