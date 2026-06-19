// (int)(unsigned char)-1 = 255
// 期待: exit=255
#include <assert.h>
int main(void) {
    assert((int)(unsigned char)-1 == 255);
    return 0;
}
