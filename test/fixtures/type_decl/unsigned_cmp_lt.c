// unsigned int の比較で MSB ある値 > 1
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int a = (1u << 31) | 1;
    assert((a > 1u) ? 1 : 0);
    return 0;
}
