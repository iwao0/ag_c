// if 条件が偽のとき else 側の return が実行される
// 期待: exit=0
#include <assert.h>
int main(void) {
    int a = 3;
    int r;
    if (a == 5) r = a;
    else r = 0;
    assert(r == 0);
    return 0;
}
