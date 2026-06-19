// while ループの基本動作
// 期待: exit=10
#include <assert.h>
int main(void) {
    int a = 0;
    while (a < 10) a = a + 1;
    assert(a == 10);
    return 0;
}
