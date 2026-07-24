#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int format_with_va_list(char *buffer, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int main(void) {
    char buffer[96];

    if (snprintf(buffer, sizeof(buffer), "%hhd/%hhu", 255, 258) != 4) return 1;
    if (strcmp(buffer, "-1/2") != 0) return 2;

    if (snprintf(buffer, sizeof(buffer), "%hd/%hu", 65535, 65538) != 4) return 3;
    if (strcmp(buffer, "-1/2") != 0) return 4;

    intmax_t intmax_value = -5000000000123LL;
    uintmax_t uintmax_value = 18000000000000000000ULL;
    if (snprintf(buffer, sizeof(buffer), "%jd/%ju",
                 intmax_value, uintmax_value) != 35) return 5;
    if (strcmp(buffer, "-5000000000123/18000000000000000000") != 0) return 6;

    size_t size_value = 5000000000UL;
    ptrdiff_t difference_value = -4000000000L;
    if (snprintf(buffer, sizeof(buffer), "%zu/%td",
                 size_value, difference_value) != 22) return 7;
    if (strcmp(buffer, "5000000000/-4000000000") != 0) return 8;

    if (format_with_va_list(buffer, sizeof(buffer), "%hhd:%hu:%jd:%zu:%td",
                            255, 65538, intmax_value,
                            size_value, difference_value) != 42) return 9;
    if (strcmp(buffer, "-1:2:-5000000000123:5000000000:-4000000000") != 0) return 10;

    signed char hhn_slots[4] = {7, -1, 8, 9};
    if (snprintf(buffer, sizeof(buffer), "abc%hhn", &hhn_slots[1]) != 3) return 11;
    if (strcmp(buffer, "abc") != 0 ||
        hhn_slots[0] != 7 || hhn_slots[1] != 3 ||
        hhn_slots[2] != 8 || hhn_slots[3] != 9) return 12;

    short hn_slots[3] = {123, -1, 456};
    if (snprintf(buffer, sizeof(buffer), "wxyz%hn", &hn_slots[1]) != 4) return 13;
    if (strcmp(buffer, "wxyz") != 0 ||
        hn_slots[0] != 123 || hn_slots[1] != 4 ||
        hn_slots[2] != 456) return 14;

    intmax_t j_count = -1;
    long z_count = -1;
    ptrdiff_t t_count = -1;
    if (format_with_va_list(buffer, sizeof(buffer), "AB%jnCD%znEF%tn",
                            &j_count, &z_count, &t_count) != 6) return 15;
    if (strcmp(buffer, "ABCDEF") != 0 ||
        j_count != 2 || z_count != 4 || t_count != 6) return 16;

    if (snprintf(buffer, sizeof(buffer), "%d/%u/%d", -7, 8u, -9) != 7) return 17;
    if (strcmp(buffer, "-7/8/-9") != 0) return 18;

    return 0;
}
