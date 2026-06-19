// グローバル配列の部分初期化。残り要素は 0 で埋められる。
// g = {1, 2, 3, 0, 0}
// 期待: exit=0
#include <assert.h>
int g[5] = {1, 2, 3};
int main(void) {
    assert(g[0] == 1);
    assert(g[2] == 3);
    assert(g[4] == 0);
    return 0;
}
