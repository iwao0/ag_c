// グローバル struct の brace 初期化 + メンバアクセス
// struct P p = {10, 32}; → p.x = 10, p.y = 32、合計 = 42
// 期待: exit=42
#include <assert.h>
struct P { int x; int y; };
struct P p = {10, 32};
int main(void) {
    assert(p.x == 10);
    assert(p.y == 32);
    return 0;
}
