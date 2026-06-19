// unsigned int の 0-1 ラップ (=0xFFFFFFFF)
// 期待: exit=2 (1+1)
#include <assert.h>
int main(void) {
    unsigned int x = 0;
    x = x - 1;
    assert((x > 0) + (x == 4294967295u) == 2);
    return 0;
}
