// UCN を含む識別子 (ñ = U+00F1)
// 期待: exit=7
#include <assert.h>
int main(void) {
    int \u00f1 = 7;
    assert(\u00f1 == 7);
    return 0;
}
