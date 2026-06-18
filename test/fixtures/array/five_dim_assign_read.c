// 5 次元配列アクセス (4D 対応と同じ汎用ロジックで動く)
// 期待: exit=77
#include <assert.h>
int main(void) {
    int a[2][2][2][2][3];
    a[1][1][1][1][2] = 77;
    assert(a[1][1][1][1][2] == 77);
    return 0;
}
