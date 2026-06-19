// 配列名は式中で pointer に decay する。&a[9] - a は要素差 (= 9) になる。
// 期待: exit=9
#include <assert.h>
int main(void) {
    int a[10];
    assert((int)(&a[9] - a) == 9);
    return 0;
}
