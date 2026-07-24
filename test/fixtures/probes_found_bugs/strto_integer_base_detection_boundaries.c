#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

int main(void) {
    char *end = NULL;

    char octal[] = "077tail";
    if (strtol(octal, &end, 0) != 63 || end != octal + 3) return 1;

    char negative_hex[] = "-0X2Az";
    if (strtol(negative_hex, &end, 0) != -42 ||
        end != negative_hex + 5) return 2;

    char unsigned_hex[] = "0xff!";
    if (strtoul(unsigned_hex, &end, 0) != 255UL ||
        end != unsigned_hex + 4) return 3;

    char unsigned_octal[] = "010q";
    if (strtoull(unsigned_octal, &end, 0) != 8ULL ||
        end != unsigned_octal + 3) return 4;

    char maximum_base[] = "z!";
    if (strtoimax(maximum_base, &end, 36) != 35 ||
        end != maximum_base + 1) return 5;

    char decimal[] = "99x";
    if (strtoumax(decimal, &end, 0) != 99 ||
        end != decimal + 2) return 6;

    wchar_t *wide_end = NULL;

    wchar_t wide_octal[] = L"075x";
    if (wcstol(wide_octal, &wide_end, 0) != 61 ||
        wide_end != wide_octal + 3) return 7;

    wchar_t wide_hex[] = L"0X20!";
    if (wcstoul(wide_hex, &wide_end, 0) != 32UL ||
        wide_end != wide_hex + 4) return 8;

    wchar_t wide_negative_octal[] = L"-011r";
    if (wcstoll(wide_negative_octal, &wide_end, 0) != -9 ||
        wide_end != wide_negative_octal + 4) return 9;

    wchar_t wide_unsigned_hex[] = L"0x40?";
    if (wcstoull(wide_unsigned_hex, &wide_end, 0) != 64ULL ||
        wide_end != wide_unsigned_hex + 4) return 10;

    return 0;
}
