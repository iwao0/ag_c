// 短絡評価: || の左が真なら右は評価されない
// 期待: exit=2
#include <assert.h>
int main(void) {
    int a = 1;
    int b = 0;
    if (a || (b = 1)) b = b + 2;
    assert(b == 2);
    return 0;
}
