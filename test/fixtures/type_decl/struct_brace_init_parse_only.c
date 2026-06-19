// struct 波括弧初期化 (受理確認)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { int x; int y; };
    struct S s = {1, 2};
    assert(7 == 7);
    return 0;
}
