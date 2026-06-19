// 構造体パディング込み sizeof
// char a + padding + int b + char c + padding = 12 (4 byte alignment)
// 期待: exit=12
#include <assert.h>
int main(void) {
    struct S { char a; int b; char c; };
    assert(sizeof(struct S) == 12);
    return 0;
}
