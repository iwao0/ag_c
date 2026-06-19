// `int const x` (後置 const 修飾)
// 期待: exit=42
#include <assert.h>
int main(void) {
    int const x = 42;
    assert(x == 42);
    return 0;
}
