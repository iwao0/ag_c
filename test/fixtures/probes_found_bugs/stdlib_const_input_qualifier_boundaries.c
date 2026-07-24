#include <inttypes.h>
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>

int main(void) {
    const char decimal[] = "-42x";
    const char floating[] = "3.5!";
    char *end = NULL;

    if (atoi(decimal) != -42) return 1;
    if (atol(decimal) != -42L) return 2;
    if (atoll(decimal) != -42LL) return 3;
    if (atof(floating) != 3.5) return 4;

    if (strtol(decimal, &end, 10) != -42L || end != decimal + 3) return 5;
    if (strtoul(decimal + 1, &end, 10) != 42UL ||
        end != decimal + 3) return 6;
    if (strtoll(decimal, &end, 10) != -42LL || end != decimal + 3) return 7;
    if (strtoull(decimal + 1, &end, 10) != 42ULL ||
        end != decimal + 3) return 8;
    if (strtof(floating, &end) != 3.5f || end != floating + 3) return 9;
    if (strtod(floating, &end) != 3.5 || end != floating + 3) return 10;
    if (strtold(floating, &end) != 3.5L || end != floating + 3) return 11;
    if (strtoimax(decimal, &end, 10) != -42 || end != decimal + 3) return 12;
    if (strtoumax(decimal + 1, &end, 10) != 42 ||
        end != decimal + 3) return 13;

    const wchar_t wide_decimal[] = L"-42x";
    const wchar_t wide_floating[] = L"3.5!";
    wchar_t *wide_end = NULL;

    if (wcstol(wide_decimal, &wide_end, 10) != -42L ||
        wide_end != wide_decimal + 3) return 14;
    if (wcstoul(wide_decimal + 1, &wide_end, 10) != 42UL ||
        wide_end != wide_decimal + 3) return 15;
    if (wcstoll(wide_decimal, &wide_end, 10) != -42LL ||
        wide_end != wide_decimal + 3) return 16;
    if (wcstoull(wide_decimal + 1, &wide_end, 10) != 42ULL ||
        wide_end != wide_decimal + 3) return 17;
    if (wcstof(wide_floating, &wide_end) != 3.5f ||
        wide_end != wide_floating + 3) return 18;
    if (wcstod(wide_floating, &wide_end) != 3.5 ||
        wide_end != wide_floating + 3) return 19;
    if (wcstold(wide_floating, &wide_end) != 3.5L ||
        wide_end != wide_floating + 3) return 20;

    return 0;
}
