// struct の配列メンバ波括弧省略 (a[0]=1, a[1]=2, z=3)
// 期待: exit=3
#include <assert.h>
int main(void) {
    struct S { int a[2]; int z; };
    struct S s = {1, 2, 3};
    assert(s.z == 3);
    return 0;
}
