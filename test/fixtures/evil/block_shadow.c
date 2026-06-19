// ブロックスコープの変数シャドウイング (内側 x は外側に影響しない)
// 期待: exit=10
#include <assert.h>
int main(void) {
    int x = 10;
    { int x = 20; }
    assert(x == 10);
    return 0;
}
