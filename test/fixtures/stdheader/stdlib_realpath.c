// stdlib.h realpath minimal call
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>

int main(void) {
    char buf[512];
    char *p = realpath(".", buf);
    assert(p == buf);
    assert(buf[0] != 0);
    return 0;
}
