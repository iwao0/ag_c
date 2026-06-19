// 短絡評価: && の左が偽なら右は評価されない
// 期待: exit=0 (b は変更されない)
#include <assert.h>
int main(void) {
    int a = 0;
    int b = 0;
    if (a && (b = 1)) b = 2;
    assert(b == 0);
    return 0;
}
