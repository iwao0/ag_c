// limits.h の INT_MAX
// 期待: exit=42
#include <limits.h>
#include <assert.h>
int main(void) {
    assert(INT_MAX == 2147483647);
    return 0;
}
