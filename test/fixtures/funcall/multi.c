// 関数呼び出しを引数の中でネスト
// add(mul(3,4), mul(3,3)) = add(12, 9) = 21
// 期待: exit=21
#include <assert.h>
add(a, b) { return a + b; }
mul(a, b) { return a * b; }
main() { assert(add(mul(3, 4), mul(3, 3)) == 21); return 0; }
