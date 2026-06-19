// struct 複合リテラルを文として書く (副作用なし)
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int x; int y; };
    (struct S){1, 2};
    assert(1 == 1);
    return 0;
}
