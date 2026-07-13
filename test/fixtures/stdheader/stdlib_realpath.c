// stdlib.h realpath minimal call
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>

int main(void) {
    /* Native E2E must not depend on host filesystem policy. */
    char *(*resolve_path)(const char *, char *) = realpath;
    assert(resolve_path != 0);
    return 0;
}
