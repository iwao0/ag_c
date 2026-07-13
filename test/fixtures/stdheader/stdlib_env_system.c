// stdlib.h environment/system minimal calls
// Expected: exit=0
#include <assert.h>
#include <errno.h>
#include <stdlib.h>

static void cleanup(void) {
}

int main(void) {
    assert(atexit((void *)cleanup) == 0);
    assert(getenv("AG_C_E2E_EXPECT_THIS_ENV_TO_BE_MISSING") == 0);
    assert(system(0) == 0);
    errno = 0;
    assert(system("") == -1);
    assert(errno == ENOSYS);
    return 0;
}
