// limits.h の INT_MIN は負数
// 期待: exit=42
#include <limits.h>
#include <assert.h>
int main(void) {
    assert(INT_MIN < 0);
    return 0;
}
