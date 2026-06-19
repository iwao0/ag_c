// case 1 から case 2 にフォールスルー、 break で抜ける
// b = 0+1+2 = 3
// 期待: exit=3
#include <assert.h>
int main(void) {
    int a = 1;
    int b = 0;
    switch (a) {
        case 1: b = b + 1;
        case 2: b = b + 2; break;
        default: b = 99;
    }
    assert(b == 3);
    return 0;
}
