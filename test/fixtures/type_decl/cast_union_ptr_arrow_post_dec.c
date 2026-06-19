// union ポインタの -> 経由で後置デクリメント
// a[1]: 2→1, a[0]: 1 → 1+1 = 2
// 期待: exit=2
#include <assert.h>
int main(void) {
    union U { int a[2]; int z; };
    union U u = {1, 2};
    ((union U*)&u)->a[1]--;
    assert(u.a[0] == 1);
    assert(u.a[1] == 1);   // 2-- -> 1
    return 0;
}
