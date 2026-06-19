// unsigned long と unsigned int の区別
// 期待: exit=0
#include <assert.h>
int main(void) {
    unsigned long ul = 1;
    assert(_Generic(ul, unsigned long: 2, unsigned int: 1, default: 3) == 2);
    return 0;
}
