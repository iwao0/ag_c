// WAT standalone errno storage through __error()
// expected: exit=0
#include <errno.h>

int main(void) {
    int *p = __error();
    int *(*perrno)(void) = __error;

    if (!p) return 1;
    errno = 0;
    if (*p != 0) return 2;
    *p = EDOM;
    if (errno != EDOM) return 3;
    errno = ERANGE;
    if (*perrno() != ERANGE) return 4;
    if (perrno() != p) return 5;
    return 0;
}
