// signed char で 127+1 == -128
// 期待: exit=1
#include <assert.h>
int main(void) {
    char c = 127;
    c = c + 1;
    assert(c == -128);
    return 0;
}
