// _Alignas を付けたローカル変数で値が正しく読めること
// 期待: exit=42
#include <assert.h>
int main(void) {
    _Alignas(16) int a = 42;
    assert(a == 42);
    return 0;
}
