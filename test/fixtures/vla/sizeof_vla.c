// VLA に対する sizeof は実行時に値を返す (n * sizeof(int) = 3*4 = 12)
// 期待: exit=12
#include <assert.h>
int main(void) {
    int n = 3;
    int a[n];
    assert((int)sizeof(a) == 12);
    return 0;
}
