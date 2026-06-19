// unsigned int の <= 比較
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int a = (1u << 31) | 1;
    unsigned int b = a;
    assert((a <= b) ? 1 : 0);
    return 0;
}
