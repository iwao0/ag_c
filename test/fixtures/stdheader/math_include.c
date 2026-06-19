// math.h のインクルードが通ること
// 期待: exit=42
#include <math.h>
#include <assert.h>
int main(void) {
    assert(42 == 42);
    return 0;
}
