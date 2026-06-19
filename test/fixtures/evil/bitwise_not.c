// ~0xFF & 0xFF = 0
// 期待: exit=0
#include <assert.h>
int main(void) {
    int x = 0xFF;
    assert((~x & 0xFF) == 0);
    return 0;
}
