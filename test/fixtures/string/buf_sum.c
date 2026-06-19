// char バッファの合計
// 期待: exit=6
#include <assert.h>
int main(void) {
    char buf[3];
    buf[0] = 1;
    buf[1] = 2;
    buf[2] = 3;
    assert(buf[0] == 1);
    assert(buf[1] == 2);
    assert(buf[2] == 3);
    return 0;
}
