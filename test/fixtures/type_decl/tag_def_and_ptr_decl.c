// タグ定義と同時にポインタ変数宣言
// 期待: exit=1
#include <assert.h>
int main(void) {
    struct S { int x; } *p;
    p = 0;
    assert(p == 0);
    return 0;
}
