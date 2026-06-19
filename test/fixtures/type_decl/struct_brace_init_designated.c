// struct の指定初期化子 (順不同)
// 期待: exit=3 (1+2)
#include <assert.h>
int main(void) {
    struct S { int x; int y; };
    struct S s = {.y = 2, .x = 1};
    assert(s.x == 1);
    assert(s.y == 2);
    return 0;
}
