// union の配列メンバを非波括弧で初期化 (ag_c 拡張)
// a[0]=1, a[1]=2 → 3
// 期待: exit=3
#include <assert.h>
int main(void) {
    union U { int a[2]; int z; };
    union U u = {1, 2};
    assert(u.a[0] == 1);
    assert(u.a[1] == 2);
    return 0;
}
