// 初期化済みグローバル変数
// 期待: exit=42
#include <assert.h>
int g = 42;
int main(void) {
    assert(g == 42);
    return 0;
}
