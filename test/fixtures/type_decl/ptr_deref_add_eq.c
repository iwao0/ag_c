// ポインタ経由の複合代入 *p += 2
// 期待: exit=7
#include <assert.h>
int main(void) {
    int x = 5;
    int *p = &x;
    *p += 2;
    assert(x == 7);
    return 0;
}
