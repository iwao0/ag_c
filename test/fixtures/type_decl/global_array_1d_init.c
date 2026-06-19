// グローバル 1D 配列の brace 初期化子。
// g[2] = 30
// 期待: exit=0
#include <assert.h>
int g[3] = {10, 20, 30};
int main(void) {
    assert(g[2] == 30);
    return 0;
}
