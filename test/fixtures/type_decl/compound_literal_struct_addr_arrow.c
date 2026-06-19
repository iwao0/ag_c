// & + struct 複合リテラル + ->
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int x; };
    assert((&(struct S){3})->x == 3);
    return 0;
}
