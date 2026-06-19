// 仮引数でも修飾子の重複を受理
// 3+4+(0==0)=8
// 期待: exit=8
#include <assert.h>
int sumq(const const int a, volatile volatile int b, int *restrict restrict p) {
    return a + b + (p == 0);
}
int main(void) { assert(sumq(3, 4, 0) == 8); return 0; }
