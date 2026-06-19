// do-while + continue (奇数のみ加算)
// 1+3+5 = 9
// 期待: exit=9
#include <assert.h>
int main(void) {
    int s = 0;
    int i = 0;
    do {
        i = i + 1;
        if (i % 2 == 0) continue;
        s = s + i;
    } while (i < 6);
    assert(s == 9);
    return 0;
}
