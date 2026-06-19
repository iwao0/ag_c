// 仮引数 VLA 経由で呼び出し元の配列に書き込む
// 14*3 = 42
// 期待: exit=42
#include <assert.h>
void fill(int n, int a[n], int v) {
    int i;
    for (i = 0; i < n; i++) a[i] = v;
}
int main(void) {
    int n = 3;
    int a[n];
    fill(n, a, 14);
    assert(a[0] == 14);
    assert(a[1] == 14);
    assert(a[2] == 14);
    return 0;
}
