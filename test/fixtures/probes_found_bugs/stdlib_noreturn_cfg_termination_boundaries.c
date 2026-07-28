/*
 * C11 process-termination APIs and the assertion failure implementation are
 * direct _Noreturn declarations.  Their calls terminate the reachable CFG.
 * E2E calls use an early return so no process is actually terminated.
 */
#include <assert.h>
#include <stdlib.h>

static int direct_exit(int value) {
    if (value == 42) return 42;
    exit(0);
}

static int parenthesized_exit(int value) {
    if (value == 42) return 42;
    (exit)(0);
}

static int direct_abort(int value) {
    if (value == 42) return 42;
    abort();
}

static int direct__Exit(int value) {
    if (value == 42) return 42;
    _Exit(0);
}

static int direct_quick_exit(int value) {
    if (value == 42) return 42;
    quick_exit(0);
}

static int constant_assert_failure(int value) {
    if (value == 42) return 42;
    assert(0);
}

int main(void) {
    if (direct_exit(42) != 42) return 1;
    if (parenthesized_exit(42) != 42) return 2;
    if (direct_abort(42) != 42) return 3;
    if (direct__Exit(42) != 42) return 4;
    if (direct_quick_exit(42) != 42) return 5;
    if (constant_assert_failure(42) != 42) return 6;
    return 0;
}
