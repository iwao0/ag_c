// long と unsigned long の区別
// 期待: exit=0
#include <assert.h>
int main(void) {
    long l = 1;
    assert(_Generic(l, unsigned long: 1, long: 2, default: 3) == 2);
    return 0;
}
