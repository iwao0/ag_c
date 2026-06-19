// _Alignof(struct S) = 4
// 期待: exit=4
#include <assert.h>
int main(void) {
    struct S { int x; };
    assert(_Alignof(struct S) == 4);
    return 0;
}
