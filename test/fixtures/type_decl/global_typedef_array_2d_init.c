// グローバル typedef 多次元配列の brace 初期化子。
// g[1][2] = 60
// 期待: exit=0
#include <assert.h>
typedef int M2[2][3];
M2 g = {{10, 20, 30}, {40, 50, 60}};
int main(void) {
    assert(g[1][2] == 60);
    return 0;
}
