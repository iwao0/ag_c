// グローバル配列の合計
// 期待: exit=6
#include <assert.h>
int g[3];
int main(void) {
    g[0] = 1;
    g[1] = 2;
    g[2] = 3;
    assert(g[0] == 1);
    assert(g[1] == 2);
    assert(g[2] == 3);
    return 0;
}
