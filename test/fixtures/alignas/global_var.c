// グローバル変数への _Alignas(16) でアドレスが 16 の倍数になること
// 期待: exit=7 (満たさなければ 0)
#include <assert.h>
_Alignas(16) int g = 7;
int main(void) {
    long addr = (long)&g;
    assert(addr % 16 == 0);
    assert(g == 7);
    return 0;
}
