// Wasm standalone stdlib.h aligned_alloc/llabs/at_quick_exit stubs
// Expected: exit=0
#include <stdint.h>
#include <stdlib.h>

static void cleanup(void) {
}

int main(void) {
    char *p = aligned_alloc(16, 32);
    char *q = aligned_alloc(32, 32);
    if (!p || !q) return 1;
    if (((uintptr_t)p & 15) != 0) return 2;
    if (((uintptr_t)q & 31) != 0) return 3;
    p[0] = 11;
    p[31] = 22;
    q[0] = 33;
    q[31] = 44;
    if (p[0] != 11 || p[31] != 22 || q[0] != 33 || q[31] != 44) return 4;
    if (aligned_alloc(0, 8) != 0) return 5;
    if (aligned_alloc(12, 24) != 0) return 6;
    if (aligned_alloc(16, 24) != 0) return 7;

    if (llabs(-1234567890123LL) != 1234567890123LL) return 8;
    if (llabs(55LL) != 55LL) return 9;
    if (at_quick_exit((void *)cleanup) != 0) return 10;
    return 0;
}
