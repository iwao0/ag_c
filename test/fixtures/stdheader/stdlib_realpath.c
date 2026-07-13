// stdlib.h realpath minimal call
// Expected: exit=0
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

int main(void) {
    char buf[512];
    errno = 0;
    char *p = realpath(".", buf);
    assert(p == 0);
    assert(errno == ENOSYS);
    return 0;
}
