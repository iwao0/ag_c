/*
 * _Noreturn belongs to a direct function declaration, not its function type.
 * A conditional successful assertion and an indirect terminator call can
 * still reach the closing brace, so W3005 must remain.
 */
#include <assert.h>
#include <stdlib.h>

static int conditional_assert_can_continue(int value) {
    assert(value);
}

static int indirect_exit_can_continue(void (*terminate)(int)) {
    terminate(0);
}

static int constant_assert_success_can_continue(void) {
    assert(1);
}

int main(void) {
    return 0;
}
