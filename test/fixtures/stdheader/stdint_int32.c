// stdint.h の int32_t 型
// 期待: exit=42
#include <stdint.h>
#include <assert.h>
int main(void) {
    int32_t x = 42;
    assert(x == 42);
    return 0;
}
