// (struct S*)p で NULL ポインタのキャスト
// 期待: exit=1
#include <assert.h>
int main(void) {
    struct S { int x; };
    struct S *p = 0;
    assert(((struct S*)p) == 0);
    return 0;
}
