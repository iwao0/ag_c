// unsigned int >> 31 で最上位 bit が 1 になる
// 期待: exit=1
#include <assert.h>
int main(void) {
    unsigned int a = 0x80000000;
    assert((a >> 31) == 1);
    return 0;
}
