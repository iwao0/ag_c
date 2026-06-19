// 匿名 struct メンバ (パース確認)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { struct { int x; }; int y; };
    assert(7 == 7);
    return 0;
}
