/*
 * C11 declares longjmp as _Noreturn.  Direct and parenthesized calls must
 * terminate the caller's reachable CFG without changing its function type.
 */
#include <setjmp.h>

static int direct_longjmp(int value, jmp_buf env) {
    if (value) return value;
    longjmp(env, 1);
}

static int parenthesized_longjmp(int value, jmp_buf env) {
    if (value) return value;
    (longjmp)(env, 1);
}

int main(void) {
    jmp_buf env;

    /* Keep the non-local transfer paths compiled but do not execute them. */
    if (direct_longjmp(42, env) != 42) return 1;
    if (parenthesized_longjmp(42, env) != 42) return 2;
    return 0;
}
