// unsigned char に 255 → 255 (ゼロ拡張)
// 期待: exit=255
#include <assert.h>
int main(void) {
    unsigned char c = 255;
    assert(c == 255);
    return 0;
}
