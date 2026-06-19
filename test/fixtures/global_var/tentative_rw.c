// グローバル変数の暫定定義への書き込み + 読み出し
// 期待: exit=42
#include <assert.h>
int g;
int main(void) {
    g = 42;
    assert(g == 42);
    return 0;
}
