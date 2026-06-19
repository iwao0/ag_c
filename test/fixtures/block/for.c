// for ループの本体にブロック
// 期待: exit=55
#include <assert.h>
int main(void) {
    int a;
    int b = 0;
    for (a = 1; a <= 10; a = a + 1) { b = b + a; }
    assert(b == 55);
    return 0;
}
