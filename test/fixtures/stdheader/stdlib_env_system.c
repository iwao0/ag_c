// stdlib.h environment/system minimal calls
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>

static void cleanup(void) {
}

int main(void) {
    assert(atexit((void *)cleanup) == 0);
    assert(getenv("AG_C_E2E_EXPECT_THIS_ENV_TO_BE_MISSING") == 0);
    /* Native E2E validates the declaration and linkage only. Executing a
       command processor is host policy and belongs outside compiler tests. */
    int (*run_command)(const char *) = system;
    assert(run_command != 0);
    return 0;
}
