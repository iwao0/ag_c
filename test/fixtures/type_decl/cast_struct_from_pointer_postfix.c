// ポインタから struct への cast (拡張)、後置 .p で deref
// 期待: exit=3
#include <assert.h>
int main(void) {
    struct S { int *p; int q; };
    int x = 3;
    assert(*((struct S)&x).p == 3);
    return 0;
}
