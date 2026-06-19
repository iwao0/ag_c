// int 引数を取る関数
// 期待: exit=10
#include <assert.h>
int add(int a, int b) { return a + b; }
int main(void) { assert(add(3, 7) == 10); return 0; }
