// 空文 + 通常文混在
// 期待: exit=3
#include <assert.h>
int main(void) {
    int x = 1;
    ;
    x = x + 2;
    ; ;
    assert(x == 3);
    return 0;
}
