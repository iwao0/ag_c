// Standard allocation, environment, and termination callback declarations.
// Expected: exit=0
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static void *(*malloc_signature)(size_t) = malloc;
static void *(*calloc_signature)(size_t, size_t) = calloc;
static void *(*realloc_signature)(void *, size_t) = realloc;
static void (*free_signature)(void *) = free;
static void *(*aligned_alloc_signature)(size_t, size_t) = aligned_alloc;
static int (*atexit_signature)(void (*)(void)) = atexit;
static int (*at_quick_exit_signature)(void (*)(void)) = at_quick_exit;
static char *(*getenv_signature)(const char *) = getenv;

static void cleanup(void) {
}

int main(void) {
    unsigned char *bytes = malloc_signature(8);
    unsigned char *zeroes = calloc_signature(4, 2);
    void *aligned;

    if (!bytes || !zeroes) return 1;
    bytes[0] = 17;
    bytes[7] = 29;
    for (size_t i = 0; i < 8; ++i) {
        if (zeroes[i] != 0) return 2;
    }

    bytes = realloc_signature(bytes, 16);
    if (!bytes || bytes[0] != 17 || bytes[7] != 29) return 3;
    bytes[15] = 41;

    aligned = aligned_alloc_signature(16, 32);
    if (!aligned || ((uintptr_t)aligned & 15) != 0) return 4;

    if (atexit_signature(cleanup) != 0) return 5;
    if (at_quick_exit_signature(cleanup) != 0) return 6;
    if (getenv_signature("AG_C_E2E_EXPECT_THIS_ENV_TO_BE_MISSING") != 0) return 7;

    free_signature(aligned);
    free_signature(zeroes);
    free_signature(bytes);
    return 0;
}
