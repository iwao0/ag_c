// while(1) + break (x = 1, 2, 4, 8, 16, 32, 64 で break)
// 期待: exit=64
#include <assert.h>
int main(void) {
    int x = 1;
    while (1) {
        if (x > 50) break;
        x = x * 2;
    }
    assert(x == 64);
    return 0;
}
