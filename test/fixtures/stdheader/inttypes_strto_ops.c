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
    const char *d8 = SCNd8;
    const char *d16 = SCNd16;
    const char *u8 = SCNu8;
    const char *x16 = SCNx16;
    const char *dmax = SCNdMAX;
    const char *xptr = SCNxPTR;
    const char *priu8 = PRIu8;
    const char *prid16 = PRId16;
    const char *prixptr = PRIXPTR;
    if (d8[0] != 'h' || d8[1] != 'h' || d8[2] != 'd' || d8[3] != 0) return 5;
    if (d16[0] != 'h' || d16[1] != 'd' || d16[2] != 0) return 6;
    if (u8[0] != 'h' || u8[1] != 'h' || u8[2] != 'u' || u8[3] != 0) return 7;
    if (x16[0] != 'h' || x16[1] != 'x' || x16[2] != 0) return 8;
    if (dmax[0] != 'l' || dmax[1] != 'l' || dmax[2] != 'd' || dmax[3] != 0) return 9;
    if (xptr[0] != 'l' || xptr[1] != 'x' || xptr[2] != 0) return 10;
    if (priu8[0] != 'h' || priu8[1] != 'h' || priu8[2] != 'u' || priu8[3] != 0) return 11;
    if (prid16[0] != 'h' || prid16[1] != 'd' || prid16[2] != 0) return 12;
    if (prixptr[0] != 'l' || prixptr[1] != 'X' || prixptr[2] != 0) return 13;
    return 0;
}
