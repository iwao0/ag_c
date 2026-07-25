// C11 errno macro contracts: required constants and modifiable int lvalue.
#include <errno.h>
#include <stddef.h>

#if EDOM <= 0 || EILSEQ <= 0 || ERANGE <= 0
#error "required errno macros must be positive integer constant expressions"
#endif

enum {
    errno_required_sum = EDOM + EILSEQ + ERANGE
};

static int errno_constant_array[
    (EDOM > 0 && EILSEQ > 0 && ERANGE > 0) ? 1 : -1
];

static int *errno_address(void) {
    return &errno;
}

int main(void) {
    int *first = errno_address();
    int *second;

    errno = EDOM;
    second = &errno;
    if (first != second || *second != EDOM) return 1;
    *first = ERANGE;
    if (errno != ERANGE) return 2;
    if (_Generic(errno, int: 1, default: 0) != 1) return 3;
    if (errno_required_sum <= 0 ||
        sizeof(errno_constant_array) != sizeof(int)) {
        return 4;
    }
    errno = 0;
    return errno;
}
