// signed char のオーバーフロー (127+1 → -128)
// 期待: exit=1
#include <assert.h>
int main(void) {
    char c = 127;
    c = c + 1;
    assert(c == -128);
    assert(c < 0);
    return 0;
}
