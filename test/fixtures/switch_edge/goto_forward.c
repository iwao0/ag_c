// 前方 goto
// 期待: exit=42
#include <assert.h>
int main() {
    int result = 0;
    goto L1;
    result = 0;
L1:
    result = 42;
    assert(result == 42);
    return 0;
}
