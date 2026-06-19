// short に 65535 → -1 (符号拡張)
// 期待: exit=1
#include <assert.h>
int main(void) {
    short s = 65535;
    assert((s < 0) ? 1 : 0);
    return 0;
}
