// _Generic で struct 型を assoc に書く (パース+選択)
// 期待: exit=0
#include <assert.h>
int main(void) {
    struct S { int x; };
    assert(_Generic((struct S){1}, struct S: 1, default: 2) == 1);
    return 0;
}
