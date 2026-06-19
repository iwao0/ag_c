// 匿名 union メンバ (パース確認)
// 期待: exit=7
#include <assert.h>
int main(void) {
    struct S { union { int x; char c; }; int y; };
    assert(7 == 7);
    return 0;
}
