// setjmp.h のインクルードが通ること
// 期待: exit=42
#include <setjmp.h>
#include <assert.h>
int main(void) {
    assert(42 == 42);
    return 0;
}
