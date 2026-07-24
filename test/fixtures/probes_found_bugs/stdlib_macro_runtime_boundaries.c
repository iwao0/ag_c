// stdlib.h required macros and their runtime implementation boundaries.
// Expected: exit=0
#include <limits.h>
#include <stdlib.h>

#ifndef RAND_MAX
#error "stdlib.h must define RAND_MAX"
#endif

#ifndef MB_CUR_MAX
#error "stdlib.h must define MB_CUR_MAX"
#endif

#if EXIT_SUCCESS != 0 || EXIT_FAILURE == EXIT_SUCCESS
#error "exit status macros must be distinct integer constant expressions"
#endif

#ifdef __wasm32__
#if RAND_MAX != 32767
#error "RAND_MAX must match the bundled Wasm LCG"
#endif
#else
#if RAND_MAX != 0x7fffffff
#error "RAND_MAX must match the native Darwin libc"
#endif
#endif

static int random_ceiling = RAND_MAX;
static int exit_statuses[(EXIT_FAILURE != EXIT_SUCCESS) ? 2 : -1];

int main(void) {
    volatile int exit_success_kind = _Generic(
        EXIT_SUCCESS, int: 1, default: 0);
    volatile int exit_failure_kind = _Generic(
        EXIT_FAILURE, int: 1, default: 0);
    volatile int rand_max_kind = _Generic(
        RAND_MAX, int: 1, default: 0);
    volatile int mb_cur_max_kind = _Generic(
        +(MB_CUR_MAX), int: 1, default: 0);
    int first;
    int second;

    if (exit_success_kind != 1 || exit_failure_kind != 1 ||
        rand_max_kind != 1 || mb_cur_max_kind != 1)
        return 1;
    if (random_ceiling != RAND_MAX ||
        sizeof(exit_statuses) != 2 * sizeof(int))
        return 2;
    if (MB_CUR_MAX < 1 || MB_CUR_MAX > MB_LEN_MAX)
        return 3;
#ifdef __wasm32__
    if (MB_CUR_MAX != 4)
        return 4;
#endif

    srand(0x12345678U);
    first = rand();
    second = rand();
    if (first < 0 || first > RAND_MAX || second < 0 || second > RAND_MAX)
        return 5;
    srand(0x12345678U);
    if (rand() != first || rand() != second)
        return 6;
    for (int i = 0; i < 128; ++i) {
        int value = rand();
        if (value < 0 || value > RAND_MAX)
            return 7;
    }
    return 0;
}
