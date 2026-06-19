// struct にビットフィールド + 通常メンバ (パース確認)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { int x:3; int y; };
    assert(7 == 7);
    return 0;
}
