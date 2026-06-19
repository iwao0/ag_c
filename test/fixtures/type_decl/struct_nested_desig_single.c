// ネスト指定初期化子 .a[1]=3
// 期待: exit=3
#include <assert.h>
int main(void) {
    struct S { int a[2]; int z; };
    struct S s = {.a[1] = 3, .z = 9};
    assert(s.a[1] == 3);
    return 0;
}
