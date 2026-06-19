// スカラから struct への cast (ag_c 拡張: 第1メンバに格納)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { int x; int y; };
    assert(((struct S)7).x == 7);
    return 0;
}
