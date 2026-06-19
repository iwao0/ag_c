// struct 波括弧初期化値の確認
// 期待: exit=3 (1+2)
#include <assert.h>
int main(void) {
    struct S { int x; int y; };
    struct S s = {1, 2};
    assert(s.x == 1);
    assert(s.y == 2);
    return 0;
}
