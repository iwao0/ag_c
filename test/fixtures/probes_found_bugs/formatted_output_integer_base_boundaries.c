#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
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

    if (snprintf(buffer, sizeof(buffer), "%#x/%#X", 0x2a, 0x2a) != 9) return 1;
    if (strcmp(buffer, "0x2a/0X2A") != 0) return 2;

    if (snprintf(buffer, sizeof(buffer), "%#o", 052) != 3) return 3;
    if (strcmp(buffer, "052") != 0) return 4;

    if (snprintf(buffer, sizeof(buffer), "%08x/%-8o", 0x2a, 052) != 17) return 5;
    if (strcmp(buffer, "0000002a/52      ") != 0) return 6;

    if (snprintf(buffer, sizeof(buffer), "%.5x/%#.5o", 0x2a, 052) != 11) return 7;
    if (strcmp(buffer, "0002a/00052") != 0) return 8;

    if (snprintf(buffer, sizeof(buffer), "%08.5x", 0x2a) != 8) return 9;
    if (strcmp(buffer, "   0002a") != 0) return 10;

    uintmax_t wide_value = UINT64_C(0x123456789abcdef0);
    size_t size_value = 012345670UL;
    if (format_with_va_list(buffer, sizeof(buffer), "%hhx:%hX:%jx:%zo",
                            0x1ff, 0x12345, wide_value, size_value) != 33) return 11;
    if (strcmp(buffer, "ff:2345:123456789abcdef0:12345670") != 0) return 12;

    void *pointer_value = (void *)(uintptr_t)0x1234;
    if (snprintf(buffer, sizeof(buffer), "%10p", pointer_value) != 10) return 13;
    if (strcmp(buffer, "    0x1234") != 0) return 14;

    if (format_with_va_list(buffer, sizeof(buffer), "%-10p/%#jx",
                            pointer_value, wide_value) != 29) return 15;
    if (strcmp(buffer, "0x1234    /0x123456789abcdef0") != 0) return 16;

    if (snprintf(buffer, sizeof(buffer), "%#08x", 0x2a) != 8) return 17;
    if (strcmp(buffer, "0x00002a") != 0) return 18;

    if (snprintf(buffer, sizeof(buffer), "%#.0x/%#.0o", 0, 0) != 2) return 19;
    if (strcmp(buffer, "/0") != 0) return 20;

    if (format_with_va_list(buffer, sizeof(buffer), "%#*.*x", 10, 5, 0x2a) != 10) return 21;
    if (strcmp(buffer, "   0x0002a") != 0) return 22;

    return 0;
}
