// ポインタ経由で読んだ後に *p = 0 で元変数も 0 化
// y=42 (旧値)、 x=0 (クリア後) → 42+0 = 42
// 期待: exit=42
#include <assert.h>
int main(void) {
    int x = 42;
    int *p = &x;
    int y = *p;
    *p = 0;
    assert(y == 42);
    assert(x == 0);
    assert(y + x == 42);
    return 0;
}
