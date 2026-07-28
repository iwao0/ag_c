/*
 * Allocation requests that cannot fit in the target address space fail
 * without turning a truncated size into a successful small allocation.
 * A failed realloc also leaves the original allocation intact.
 * Expected: exit=0
 */
#include <stddef.h>
#include <stdlib.h>

static void *(*malloc_signature)(size_t) = malloc;
static void *(*calloc_signature)(size_t, size_t) = calloc;
static void *(*realloc_signature)(void *, size_t) = realloc;
static void *(*aligned_alloc_signature)(size_t, size_t) = aligned_alloc;
static void (*free_signature)(void *) = free;

int main(void) {
    size_t maximum = (size_t)-1;
    size_t aligned_huge = maximum & ~(size_t)15;
    unsigned char *original;
    void *result;

    result = calloc_signature(0, maximum);
    free_signature(result);
    result = calloc_signature(maximum, 0);
    free_signature(result);

    if (malloc_signature(maximum) != NULL) return 1;
    if (calloc_signature(maximum, 2) != NULL) return 2;
    if (realloc_signature(NULL, maximum) != NULL) return 3;
    if (aligned_alloc_signature(16, aligned_huge) != NULL) return 4;

    original = (unsigned char *)malloc_signature(8);
    if (original == NULL) return 5;
    original[0] = 0x35;
    original[7] = 0x7a;

    result = realloc_signature(original, maximum);
    if (result != NULL) {
        free_signature(result);
        return 6;
    }
    if (original[0] != 0x35 || original[7] != 0x7a) {
        free_signature(original);
        return 7;
    }

    free_signature(original);
    return 0;
}
