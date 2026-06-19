// signed char -1 → unsigned char → unsigned int で 255
// 期待: exit=1
#include <assert.h>
int main(void) {
    char c = -1;
    unsigned int u = (unsigned int)(unsigned char)c;
    assert(u == 255);
    return 0;
}
