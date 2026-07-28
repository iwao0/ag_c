/*
 * _Noreturn is declaration metadata rather than part of a function type.
 * An indirect longjmp call therefore remains conservative for CFG analysis.
 */
#include <setjmp.h>

static int indirect_longjmp_can_continue(int value, jmp_buf env) {
    void (*jump)(jmp_buf, int) = longjmp;
    if (value) return value;
    jump(env, 1);
}

int main(void) {
    jmp_buf env;

    /* Keep the indirect call compiled but do not execute it. */
    if (indirect_longjmp_can_continue(42, env) != 42) return 1;
    return 0;
}
