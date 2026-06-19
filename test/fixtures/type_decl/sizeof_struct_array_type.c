// sizeof(struct S[3]) = 12 (= 4*3)
// 期待: exit=12
#include <assert.h>
int main(void) {
    struct S { int x; };
    assert(sizeof(struct S[3]) == 12);
    return 0;
}
