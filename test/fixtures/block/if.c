// if-else 本体にブロック (複数文 + return)
// 期待: exit=5
#include <assert.h>
int main(void) {
    int r;
    if (1) {
        int a = 2;
        int b = 3;
        r = a + b;
    } else {
        r = 0;
    }
    assert(r == 5);
    return 0;
}
