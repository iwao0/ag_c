// 関数引数名にも複数文字変数を使う
// 期待: exit=10
#include <assert.h>
add(lhs, rhs) { return lhs + rhs; }
main() { assert(add(3, 7) == 10); return 0; }
