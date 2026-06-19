// signed char に 255 → -1 (符号拡張で負)
// 期待: exit=1
#include <assert.h>
int main(void) {
    char c = 255;
    assert((c < 0) ? 1 : 0);
    return 0;
}
