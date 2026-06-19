// ローカルスコープでの `extern int g;` 宣言が同名グローバルを参照すること
// 期待: exit=42
#include <assert.h>
int g = 42;
int main(void) {
    extern int g;
    assert(g == 42);
    return 0;
}
