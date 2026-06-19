// グローバル 2D 配列の brace 初期化子。
// g[1][2] = 60
// 期待: exit=0
#include <assert.h>
int g[2][3] = {{10, 20, 30}, {40, 50, 60}};
int main(void) {
    assert(g[1][2] == 60);
    return 0;
}
