// unsigned char の加算ラップ (200+100 = 300 → mod 256 = 44)
// 期待: exit=44
#include <assert.h>
int main(void) {
    unsigned char a = 200;
    unsigned char b = 100;
    unsigned char c = a + b;
    assert(c == 44);
    return 0;
}
