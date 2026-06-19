// 関数仮引数を使った VLA サイズ指定
// 期待: exit=10 (1+2+3+4)
#include <assert.h>
int sum(int n) {
    int a[n];
    int i;
    for (i = 0; i < n; i++) a[i] = i + 1;
    int s = 0;
    for (i = 0; i < n; i++) s += a[i];
    return s;
}
int main(void) {
    assert(sum(4) == 10);
    return 0;
}
