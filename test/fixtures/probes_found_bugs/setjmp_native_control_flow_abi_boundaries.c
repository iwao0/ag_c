// setjmp.h target layout, function pointer, and non-local control boundaries.
// Expected: exit=0
#include <setjmp.h>

#ifndef __wasm32__
static jmp_buf jump_state;
static volatile int stage;
static int (*setjmp_signature)(jmp_buf) = setjmp;
static void (*longjmp_signature)(jmp_buf, int) = longjmp;
#endif

int main(void) {
#ifdef __wasm32__
    /*
     * Standalone WAT keeps an opaque save area and returns zero.  Wasm object
     * linking rejects setjmp/longjmp as unsupported compiler control flow.
     */
    if (sizeof(jmp_buf) != 384) return 1;
#else
    if (sizeof(jmp_buf) != 192) return 2;

    stage = 0;
    if (setjmp_signature(jump_state) == 0) {
        stage = 1;
        longjmp_signature(jump_state, 37);
        return 3;
    }
    if (stage != 1) return 4;

    stage = 2;
    if (setjmp_signature(jump_state) != 1) {
        if (stage == 2) {
            stage = 3;
            longjmp_signature(jump_state, 0);
        }
        return 5;
    }
    if (stage != 3) return 6;
#endif
    return 0;
}
