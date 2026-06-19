// `for(;;)` 無限ループを break で抜ける
// 期待: exit=5
#include <assert.h>
int main(void) {
    int i = 0;
    for (;;) {
        if (i >= 5) break;
        i = i + 1;
    }
    assert(i == 5);
    return 0;
}
