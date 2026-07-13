// 2 引数関数
// 期待: exit=7 (3+4)
#include <assert.h>
int add(int a, int b) { return a + b; }
int main() { assert(add(3, 4) == 7); return 0; }
