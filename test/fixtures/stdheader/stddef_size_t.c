// stddef.h の size_t 型
// 期待: exit=10
#include <stddef.h>
#include <assert.h>
int main(void) {
    size_t x = 10;
    assert((int)x == 10);
    return 0;
}
