// union の波括弧初期化 (先頭メンバ)
// 期待: exit=7
#include <assert.h>
int main(void) {
    union U { int x; char y; };
    union U u = {7};
    assert(u.x == 7);
    return 0;
}
