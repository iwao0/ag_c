// 1 引数関数
// 期待: exit=10 (5*2)
#include <assert.h>
int twice(int a) { return a * 2; }
int main() { assert(twice(5) == 10); return 0; }
