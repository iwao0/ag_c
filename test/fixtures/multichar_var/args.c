// 関数引数名にも複数文字変数を使う
// 期待: exit=10
#include <assert.h>
int add(int lhs, int rhs) { return lhs + rhs; }
int main() { assert(add(3, 7) == 10); return 0; }
