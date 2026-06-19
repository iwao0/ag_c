// union ポインタの -> 経由で後置インクリメント
// a[0]: 1→2, a[1]: 2 → 2+2 = 4
// 期待: exit=4
#include <assert.h>
int main(void) {
    union U { int a[2]; int z; };
    union U u = {1, 2};
    ((union U*)&u)->a[0]++;
    assert(u.a[0] == 2);   // 1++ -> 2
    assert(u.a[1] == 2);
    return 0;
}
