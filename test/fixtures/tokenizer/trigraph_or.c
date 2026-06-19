// トライグラフ ??! → `|`。`5 ??! 3` = `5 | 3` = 7
// 期待: exit=7
#include <assert.h>
int main(void) {
    int x = 5 ??! 3;
    assert(x == 7);
    return 0;
}
