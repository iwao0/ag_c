#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int format_with_va_list(char *buffer, size_t size,
                               const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int main(void) {
    char buffer[128];

    if (snprintf(buffer, sizeof(buffer), "%f/%F", 1.5, 2.0) != 17) return 1;
    if (strcmp(buffer, "1.500000/2.000000") != 0) return 2;

    if (snprintf(buffer, sizeof(buffer), "%+08.2f", 1.5) != 8) return 3;
    if (strcmp(buffer, "+0001.50") != 0) return 4;

    if (snprintf(buffer, sizeof(buffer), "%-8.1f", -2.5) != 8) return 5;
    if (strcmp(buffer, "-2.5    ") != 0) return 6;

    if (snprintf(buffer, sizeof(buffer), "%.2e/%.1E", 125.0, 0.25) != 16) return 7;
    if (strcmp(buffer, "1.25e+02/2.5E-01") != 0) return 8;

    if (snprintf(buffer, sizeof(buffer), "%.4g/%.3G", 12.5, 125000.0) != 13) return 9;
    if (strcmp(buffer, "12.5/1.25E+05") != 0) return 10;

    if (snprintf(buffer, sizeof(buffer), "%#.0f/%#.4g", 2.0, 12.5) != 8) return 11;
    if (strcmp(buffer, "2./12.50") != 0) return 12;

    if (snprintf(buffer, sizeof(buffer), "%.2a/%.1A", 1.5, 3.0) != 18) return 13;
    if (strcmp(buffer, "0x1.80p+0/0X1.8P+1") != 0) return 14;

    if (format_with_va_list(buffer, sizeof(buffer), "%*.*f|%.2e|%.3g",
                            -8, 2, 1.5, 0.125, 1000.0) != 23) return 15;
    if (strcmp(buffer, "1.50    |1.25e-01|1e+03") != 0) return 16;

    if (snprintf(buffer, sizeof(buffer), "%.1Lf", (long double)2.5) != 3) return 17;
    if (strcmp(buffer, "2.5") != 0) return 18;

    if (snprintf(buffer, sizeof(buffer), "%.2f/%.0e/%.1g", 9.999, 9.5, 9.5) != 17) return 19;
    if (strcmp(buffer, "10.00/1e+01/1e+01") != 0) return 20;

    if (snprintf(buffer, sizeof(buffer), "%.2g/%.2g", 0.0001, 0.00001) != 12) return 21;
    if (strcmp(buffer, "0.0001/1e-05") != 0) return 22;

    if (snprintf(buffer, sizeof(buffer), "%#.0a", 0.0) != 7) return 23;
    if (strcmp(buffer, "0x0.p+0") != 0) return 24;

    double negative_zero = -0.0;
    if (snprintf(buffer, sizeof(buffer), "%+.1f/% .1f", negative_zero, 1.0) != 9) return 25;
    if (strcmp(buffer, "-0.0/ 1.0") != 0) return 26;

    if (format_with_va_list(buffer, sizeof(buffer), "%*.*f/%.*g",
                            -8, 2, 1.5, -1, 12.5) != 13) return 27;
    if (strcmp(buffer, "1.50    /12.5") != 0) return 28;

    volatile double zero = 0.0;
    double infinity = 1.0 / zero;
    double not_a_number = zero / zero;
    if (snprintf(buffer, sizeof(buffer), "[%8f]/[%F]/[%+f]",
                 infinity, infinity, not_a_number) != 22) return 29;
    if (strcmp(buffer, "[     inf]/[INF]/[nan]") != 0) return 30;

    if (snprintf(buffer, sizeof(buffer), "[%012.2a]/[%+012.1A]", 1.5, 3.0) != 29) return 31;
    if (strcmp(buffer, "[0x0001.80p+0]/[+0X0001.8P+1]") != 0) return 32;

    if (snprintf(buffer, sizeof(buffer), "%.0f/%.0f/%.0e", 2.5, 3.5, 2.5) != 9) return 33;
    if (strcmp(buffer, "2/4/2e+00") != 0) return 34;

    return 0;
}
