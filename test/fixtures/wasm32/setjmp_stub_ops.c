// WAT standalone setjmp.h minimal stubs
// Expected: exit=0
#include <setjmp.h>

static int check_setjmp(int (*fn)(jmp_buf), jmp_buf env) {
    return fn(env);
}

int main(void) {
    jmp_buf env;
    int (*psetjmp)(jmp_buf) = setjmp;
    void (*plongjmp)(jmp_buf, int) = longjmp;

    if (setjmp(env) != 0) return 1;
    if (check_setjmp(psetjmp, env) != 0) return 2;
    if (!plongjmp) return 3;

    return 0;
}
