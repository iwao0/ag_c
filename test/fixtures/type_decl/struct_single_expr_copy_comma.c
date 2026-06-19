// カンマ式の値を struct コピー初期化に
// t.y=9 後 t={4,9} を s にコピー → s.x+s.y = 13
// 期待: exit=13
#include <assert.h>
int main(void) {
    struct S { int x; int y; };
    struct S t = {4, 5};
    struct S s = (t.y = 9, t);
    assert(s.x + s.y == 13);
    return 0;
}
