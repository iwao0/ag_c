// 複合リテラルのメンバへの代入は lvalue
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int x; };
    assert((((struct S){1}).x = 5) == 5);
    return 0;
}
