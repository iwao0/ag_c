// VLA を for ループで埋めて合計
// 期待: exit=10 (0+1+2+3+4)
#include <assert.h>
int main(void) {
    int n = 5;
    int a[n];
    int i;
    for (i = 0; i < n; i++) a[i] = i;
    assert(a[0] == 0);
    assert(a[1] == 1);
    assert(a[2] == 2);
    assert(a[3] == 3);
    assert(a[4] == 4);
    return 0;
}
