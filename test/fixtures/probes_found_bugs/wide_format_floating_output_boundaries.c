#include <stdarg.h>
#include <stddef.h>
#include <wchar.h>

static int format_with_va_list(wchar_t *buffer, size_t size,
                               const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vswprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int main(void) {
    wchar_t buffer[128];

    if (swprintf(buffer, 128, L"%f/%F", 1.5, 2.0) != 17) return 1;
    if (wcscmp(buffer, L"1.500000/2.000000") != 0) return 2;

    if (swprintf(buffer, 128, L"%+08.2f", 1.5) != 8) return 3;
    if (wcscmp(buffer, L"+0001.50") != 0) return 4;

    if (swprintf(buffer, 128, L"%-8.1f", -2.5) != 8) return 5;
    if (wcscmp(buffer, L"-2.5    ") != 0) return 6;

    if (swprintf(buffer, 128, L"%.2e/%.1E", 125.0, 0.25) != 16) return 7;
    if (wcscmp(buffer, L"1.25e+02/2.5E-01") != 0) return 8;

    if (swprintf(buffer, 128, L"%.4g/%.3G", 12.5, 125000.0) != 13) return 9;
    if (wcscmp(buffer, L"12.5/1.25E+05") != 0) return 10;

    if (swprintf(buffer, 128, L"%#.0f/%#.4g", 2.0, 12.5) != 8) return 11;
    if (wcscmp(buffer, L"2./12.50") != 0) return 12;

    if (swprintf(buffer, 128, L"%.2a/%.1A", 1.5, 3.0) != 18) return 13;
    if (wcscmp(buffer, L"0x1.80p+0/0X1.8P+1") != 0) return 14;

    if (format_with_va_list(buffer, 128, L"%*.*f|%.2e|%.3g",
                            -8, 2, 1.5, 0.125, 1000.0) != 23) return 15;
    if (wcscmp(buffer, L"1.50    |1.25e-01|1e+03") != 0) return 16;

    if (swprintf(buffer, 128, L"%.1Lf", (long double)2.5) != 3) return 17;
    if (wcscmp(buffer, L"2.5") != 0) return 18;

    if (swprintf(buffer, 128, L"%.2f/%.0e/%.1g", 9.999, 9.5, 9.5) != 17) return 19;
    if (wcscmp(buffer, L"10.00/1e+01/1e+01") != 0) return 20;

    if (swprintf(buffer, 128, L"%.2g/%.2g", 0.0001, 0.00001) != 12) return 21;
    if (wcscmp(buffer, L"0.0001/1e-05") != 0) return 22;

    if (swprintf(buffer, 128, L"%#.0a", 0.0) != 7) return 23;
    if (wcscmp(buffer, L"0x0.p+0") != 0) return 24;

    double negative_zero = -0.0;
    if (swprintf(buffer, 128, L"%+.1f/% .1f", negative_zero, 1.0) != 9) return 25;
    if (wcscmp(buffer, L"-0.0/ 1.0") != 0) return 26;

    if (format_with_va_list(buffer, 128, L"%*.*f/%.*g",
                            -8, 2, 1.5, -1, 12.5) != 13) return 27;
    if (wcscmp(buffer, L"1.50    /12.5") != 0) return 28;

    volatile double zero = 0.0;
    double infinity = 1.0 / zero;
    double not_a_number = zero / zero;
    if (swprintf(buffer, 128, L"[%8f]/[%F]/[%+f]",
                 infinity, infinity, not_a_number) != 22) return 29;
    if (wcscmp(buffer, L"[     inf]/[INF]/[nan]") != 0) return 30;

    if (swprintf(buffer, 128, L"[%012.2a]/[%+012.1A]", 1.5, 3.0) != 29) return 31;
    if (wcscmp(buffer, L"[0x0001.80p+0]/[+0X0001.8P+1]") != 0) return 32;

    if (swprintf(buffer, 128, L"%.0f/%.0f/%.0e", 2.5, 3.5, 2.5) != 9) return 33;
    if (wcscmp(buffer, L"2/4/2e+00") != 0) return 34;

    return 0;
}
