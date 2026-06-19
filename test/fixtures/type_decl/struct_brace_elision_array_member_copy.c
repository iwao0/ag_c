// struct の配列メンバを別配列でコピー初期化 (ag_c 拡張)
// a={5,6}, z=7 → 18
// 期待: exit=18
#include <assert.h>
int main(void) {
    int src[2] = {5, 6};
    struct S { int a[2]; int z; };
    struct S s = {src, 7};
    assert(s.a[0] == 5);
    assert(s.a[1] == 6);
    assert(s.z == 7);
    return 0;
}
