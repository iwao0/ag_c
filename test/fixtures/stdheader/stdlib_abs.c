// stdlib.h の abs
// 期待: exit=42
#include <stdlib.h>
#include <assert.h>
int main(void) {
    assert(abs(-42) == 42);
    return 0;
}
