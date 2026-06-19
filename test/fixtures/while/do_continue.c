// do-while 内の continue
// a が 3, 4 のときだけ b += a → 3 + 4 = 7
// 期待: exit=7
#include <assert.h>
int main(void) {
    int a = 0;
    int b = 0;
    do {
        a = a + 1;
        if (a < 3) continue;
        b = b + a;
    } while (a < 4);
    assert(b == 7);
    return 0;
}
