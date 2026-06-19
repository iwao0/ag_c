// 2 引数関数
// 期待: exit=7 (3+4)
#include <assert.h>
add(a, b) { return a + b; }
main() { assert(add(3, 4) == 7); return 0; }
