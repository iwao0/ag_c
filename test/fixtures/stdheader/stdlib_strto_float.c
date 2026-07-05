// stdlib.h strtod/atof decimal conversion
// Expected: exit=0
#include <assert.h>
#include <stdlib.h>

int main(void) {
    char *end = 0;
    double a = strtod(" -12.5x", &end);
    assert(a == -12.5);
    assert(*end == 'x');

    char sci[] = "1.25e2!";
    double b = strtod(sci, &end);
    assert(b == 125.0);
    assert(end == sci + 6);

    assert(strtod("4e-1z", &end) == 0.4);
    assert(*end == 'z');
    assert(atof("+3.5") == 3.5);

    float f = strtof("2.5f", &end);
    assert(f == 2.5f);
    assert(*end == 'f');

    long double ld = strtold("-6.25!", &end);
    assert(ld == -6.25);
    assert(*end == '!');

    char no_digits[] = "  -.x";
    end = 0;
    assert(strtod(no_digits, &end) == 0.0);
    assert(end == no_digits);
    return 0;
}
