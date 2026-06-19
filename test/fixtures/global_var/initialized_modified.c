// 初期化済みグローバル変数を関数内で更新
// 期待: exit=42 (10+32)
#include <assert.h>
int g = 10;
int main(void) {
    g = g + 32;
    assert(g == 42);
    return 0;
}
