// 異なる struct タグで _Generic マッチしない → default
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int x; };
    struct T { int x; };
    assert(_Generic((struct S){1}, struct T: 1, default: 2) == 2);
    return 0;
}
