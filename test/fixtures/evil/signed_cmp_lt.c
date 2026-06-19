// 符号付き比較で -5 < 3 は真
// 期待: exit=1
#include <assert.h>
int main(void) {
    int x = -5;
    int y = 3;
    assert(x < y);
    return 0;
}
