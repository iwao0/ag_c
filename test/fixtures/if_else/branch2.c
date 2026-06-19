// 不等価判定で else 側へ分岐
// 期待: exit=10
#include <assert.h>
int main(void) {
    int a = 3;
    int r;
    if (a != 3) r = 5;
    else r = 10;
    assert(r == 10);
    return 0;
}
