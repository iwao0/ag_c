// sizeof(struct S) = 4 (int 1 つ)
// 期待: exit=4
#include <assert.h>
int main(void) {
    struct S { int x; };
    assert(sizeof(struct S) == 4);
    return 0;
}
