// inttypes.h strtoimax/strtoumax runtime calls
// Expected: exit=0
#include <inttypes.h>

int main(void) {
    char *end1 = 0;
    char *end2 = 0;
    intmax_t a = strtoimax(" -42x", &end1, 10);
    uintmax_t b = strtoumax("2a!", &end2, 16);
    if (a != -42) return 1;
    if (*end1 != 'x') return 2;
    if (b != 42) return 3;
    if (*end2 != '!') return 4;
    return 0;
}
