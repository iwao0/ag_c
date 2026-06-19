// 複数の関数から同じグローバル変数 g を参照
// 期待: exit=42
#include <assert.h>
int g;
int set_g(int v) { g = v; return 0; }
int main(void) {
    set_g(42);
    assert(g == 42);
    return 0;
}
