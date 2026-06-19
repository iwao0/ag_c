// 不完全 struct へのポインタ宣言
// 期待: exit=1
#include <assert.h>
int main(void) {
    struct S;
    struct S *p;
    p = 0;
    assert(p == 0);
    return 0;
}
