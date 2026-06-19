// _Alignas(16) のアラインメントが実際に取れていること (アドレス % 16 == 0)
// 期待: exit=42 (満たさなければ 0)
#include <assert.h>
int main(void) {
    int pad = 1;
    _Alignas(16) int a = 42;
    long addr = (long)&a;
    assert(addr % 16 == 0);
    assert(a == 42);
    return 0;
}
