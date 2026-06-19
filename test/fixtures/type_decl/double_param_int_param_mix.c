// 整数引数と double 引数の混在
// ARM64 ABI: 整数は x0..x7、float/double は d0..d7 と独立カウンタ
// f(2, 3.5, 4) では x0=2, d0=3.5, x1=4 と振り分けられる
// 戻り値: (3.5 * 2) + 4 = 11
// 期待: exit=11
#include <assert.h>
int f(int a, double b, int c) {
    return (int)(b * a) + c;
}
int main(void) {
    assert(f(2, 3.5, 4) == 11);
    return 0;
}
