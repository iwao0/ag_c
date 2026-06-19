// long signed 右シフトの符号保存 (-2 >> 1 = -1 = mod 256 = 255)
// 期待: exit=255
#include <assert.h>
int main(void) {
    long a = -2;
    assert((int)(a >> 1) == -1);
    return 0;
}
