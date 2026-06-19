// sizeof のオペランドは評価されない (C11 6.5.3.4p2)
// side_effect() を sizeof の中で書いても n は変わらない
// 期待: exit=1 (sizeof 後の最初の呼出は n=1)
#include <assert.h>
int n;
int side_effect(void) { return ++n; }
int main(void) {
    (void)sizeof(side_effect());
    assert(side_effect() == 1);
    return 0;
}
