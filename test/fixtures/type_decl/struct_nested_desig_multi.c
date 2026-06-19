// ネスト指定で複数要素 .a[0]=7, .a[2]=5
// 期待: exit=12
#include <assert.h>
int main(void) {
    struct S { int a[3]; };
    struct S s = {.a[0] = 7, .a[2] = 5};
    assert(s.a[0] == 7);
    assert(s.a[2] == 5);
    return 0;
}
