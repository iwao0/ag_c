// while 内の continue (a==3 のときだけスキップ)
// 1+2+4+5 = 12
// 期待: exit=12
#include <assert.h>
int main(void) {
    int a = 0;
    int b = 0;
    while (a < 5) {
        a = a + 1;
        if (a == 3) continue;
        b = b + a;
    }
    assert(b == 12);
    return 0;
}
