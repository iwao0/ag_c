// トライグラフ ??' → `^`。`5 ??' 3` = `5 ^ 3` = 6
// 期待: exit=6
#include <assert.h>
int main(void) {
    int x = 5 ??' 3;
    assert(x == 6);
    return 0;
}
