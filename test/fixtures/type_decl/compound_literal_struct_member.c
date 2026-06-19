// struct 複合リテラル + メンバアクセス
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int x; int y; };
    assert(((struct S){.x = 1, .y = 2}).y == 2);
    return 0;
}
