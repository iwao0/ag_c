// do-while + break
// 期待: exit=3
#include <assert.h>
int main(void) {
    int x = 0;
    do {
        x = x + 1;
        if (x == 3) break;
    } while (x < 10);
    assert(x == 3);
    return 0;
}
