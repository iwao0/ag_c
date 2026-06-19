// 関数ポインタ仮引数の宣言 (受理確認)
// 期待: exit=7
#include <assert.h>
int apply(int (*fp)(int), int x) { return x; }
int main(void) {
    assert(apply(0, 7) == 7);
    return 0;
}
