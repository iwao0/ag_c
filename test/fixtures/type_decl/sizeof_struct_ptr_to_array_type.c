// sizeof(struct S (*)[3]) = 8
// 期待: exit=8
#include <assert.h>
int main(void) {
    struct S { int x; };
    assert(sizeof(struct S (*)[3]) == sizeof(void*));
    return 0;
}
