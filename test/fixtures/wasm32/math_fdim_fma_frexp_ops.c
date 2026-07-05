// WAT standalone math.h fdim/fma/frexp stubs
// Expected: exit=0
#include <math.h>

int main(void) {
    int e = 99;
    float ff;
    double d;
    long double ld;

    if (fdim(7.5, 2.0) != 5.5) return 1;
    if (fdim(2.0, 7.5) != 0.0) return 2;
    if (fdimf(9.0f, 4.0f) != 5.0f) return 3;
    if (fdiml(10.0L, 1.25L) != 8.75L) return 4;

    if (fma(2.0, 3.0, 4.0) != 10.0) return 5;
    if (fmaf(1.5f, 4.0f, -1.0f) != 5.0f) return 6;
    if (fmal(2.0L, -3.0L, 1.0L) != -5.0L) return 7;

    d = frexp(8.0, &e);
    if (d != 0.5 || e != 4) return 8;
    d = frexp(-6.0, &e);
    if (d != -0.75 || e != 3) return 9;
    d = frexp(0.0, &e);
    if (d != 0.0 || e != 0) return 10;

    ff = frexpf(3.0f, &e);
    if (ff != 0.75f || e != 2) return 11;
    ld = frexpl(0.25L, &e);
    if (ld != 0.5L || e != -1) return 12;

    return 0;
}
