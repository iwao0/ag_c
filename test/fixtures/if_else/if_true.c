// if 条件が真のとき先頭の return が実行される
// 期待: exit=3
#include <assert.h>
int main(void) {
    int a = 3;
    int r;
    if (a == 3) r = a;
    else r = 0;
    assert(r == 3);
    return 0;
}
