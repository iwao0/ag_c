#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int scan_with_va_list(const char *input, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsscanf(input, format, args);
    va_end(args);
    return result;
}

int main(void) {
    char limited_word[4] = {'x', 'x', 'x', 'x'};
    char following_ch = 0;
    if (sscanf("abcdef", "%3s%c", limited_word, &following_ch) != 2) return 1;
    if (strcmp(limited_word, "abc") != 0 || following_ch != 'd') return 2;

    wchar_t limited_wide[4] = {L'x', L'x', L'x', L'x'};
    wchar_t following_wide = 0;
    if (sscanf("uvwxyz", "%3ls%lc", limited_wide, &following_wide) != 2) return 3;
    if (wcscmp(limited_wide, L"uvw") != 0 || following_wide != L'x') return 4;

    int after_suppressed = 0;
    if (sscanf("11 22", "%*d %d", &after_suppressed) != 1) return 5;
    if (after_suppressed != 22) return 6;

    int limited_integer = 0;
    char integer_tail = 0;
    if (sscanf("12345", "%3d%c", &limited_integer, &integer_tail) != 2) return 7;
    if (limited_integer != 123 || integer_tail != '4') return 8;

    char two_chars[2] = {0, 0};
    char char_tail = 0;
    if (sscanf("ABCDE", "%2c%c", two_chars, &char_tail) != 2) return 9;
    if (two_chars[0] != 'A' || two_chars[1] != 'B' || char_tail != 'C') return 10;

    unsigned signed_input = 0;
    char unsigned_tail = 0;
    if (sscanf("-17", "%2u%c", &signed_input, &unsigned_tail) != 2) return 11;
    if (signed_input != UINT_MAX || unsigned_tail != '7') return 12;

    long scanned_long = 0;
    unsigned long long scanned_ullong = 0;
    if (sscanf("-1234567890123 18000000000000000000", "%ld %llu",
               &scanned_long, &scanned_ullong) != 2) return 13;
    if (scanned_long != -1234567890123L ||
        scanned_ullong != 18000000000000000000ULL) return 14;

    limited_integer = 0;
    char va_tail = 0;
    if (scan_with_va_list("skip:789Z", "%*4c:%3d%c",
                          &limited_integer, &va_tail) != 2) return 15;
    if (limited_integer != 789 || va_tail != 'Z') return 16;

    int untouched = 91;
    if (sscanf("", "%d", &untouched) != EOF) return 17;
    if (untouched != 91) return 18;
    if (sscanf("x", "%d", &untouched) != 0) return 19;
    if (untouched != 91) return 20;
    if (sscanf("12", "%*d") != 0) return 21;
    if (sscanf("12", "%*d%d", &untouched) != EOF) return 22;
    if (untouched != 91) return 23;

    int consumed = -1;
    char count_tail = 0;
    limited_integer = 0;
    if (scan_with_va_list("789Z", "%3d%n%c",
                          &limited_integer, &consumed, &count_tail) != 2) return 24;
    if (limited_integer != 789 || consumed != 3 || count_tail != 'Z') return 25;

    long consumed_long = -1;
    char counted_chars[2] = {0, 0};
    if (sscanf("ABZ", "%2c%ln", counted_chars, &consumed_long) != 1) return 26;
    if (counted_chars[0] != 'A' || counted_chars[1] != 'B' ||
        consumed_long != 2) return 27;

    consumed = -1;
    if (sscanf("42", "%*d%n", &consumed) != 0) return 28;
    if (consumed != 2) return 29;

    unsigned scanned_hex = 0;
    unsigned scanned_octal = 0;
    if (sscanf("0x2a 075", "%x %o", &scanned_hex, &scanned_octal) != 2) return 30;
    if (scanned_hex != 42u || scanned_octal != 61u) return 31;

    int scanned_auto_hex = 0;
    int scanned_auto_octal = 0;
    if (scan_with_va_list("-0X2A 077", "%i %i",
                          &scanned_auto_hex, &scanned_auto_octal) != 2) return 32;
    if (scanned_auto_hex != -42 || scanned_auto_octal != 63) return 33;

    long scanned_auto_long = 0;
    if (sscanf("0x100000000", "%li", &scanned_auto_long) != 1) return 34;
    if (scanned_auto_long != 0x100000000L) return 35;

    int width_auto = 0;
    char width_tail = 0;
    if (sscanf("0x2fZ", "%4i%c", &width_auto, &width_tail) != 2) return 36;
    if (width_auto != 47 || width_tail != 'Z') return 37;

    void *scanned_pointer = 0;
    char pointer_tail = 0;
    if (sscanf("0X2aZ", "%p%c", &scanned_pointer, &pointer_tail) != 2) return 38;
    if (scanned_pointer != (void *)42 || pointer_tail != 'Z') return 39;

    scanned_pointer = 0;
    pointer_tail = 0;
    if (sscanf("0x2fQ", "%4p%c", &scanned_pointer, &pointer_tail) != 2) return 40;
    if (scanned_pointer != (void *)47 || pointer_tail != 'Q') return 41;

    char scan_letters[4] = {0, 0, 0, 0};
    char scan_digits[4] = {0, 0, 0, 0};
    char scan_tail = 0;
    if (scan_with_va_list("abc123Z", "%3[a-z]%3[^Z]%c",
                          scan_letters, scan_digits, &scan_tail) != 3) return 42;
    if (strcmp(scan_letters, "abc") != 0 ||
        strcmp(scan_digits, "123") != 0 || scan_tail != 'Z') return 43;

    memset(scan_digits, 0, sizeof(scan_digits));
    scan_tail = 0;
    if (sscanf("skip789Q", "%*[a-z]%3[0-9]%c",
               scan_digits, &scan_tail) != 2) return 44;
    if (strcmp(scan_digits, "789") != 0 || scan_tail != 'Q') return 45;

    wchar_t scan_wide[4] = {0, 0, 0, 0};
    scan_tail = 0;
    if (sscanf("xyzR", "%3l[a-z]%c", scan_wide, &scan_tail) != 2) return 46;
    if (wcscmp(scan_wide, L"xyz") != 0 || scan_tail != 'R') return 47;

    char scan_bracket[3] = {0, 0, 0};
    scan_tail = 0;
    if (sscanf("]-X", "%2[]-]%c", scan_bracket, &scan_tail) != 2) return 48;
    if (strcmp(scan_bracket, "]-") != 0 || scan_tail != 'X') return 49;

    scan_letters[0] = 'q';
    if (sscanf("", "%[a-z]", scan_letters) != EOF) return 50;
    if (scan_letters[0] != 'q') return 51;
    if (sscanf("7", "%[a-z]", scan_letters) != 0) return 52;
    if (scan_letters[0] != 'q') return 53;

    float scanned_float = 0.0f;
    double scanned_double = 0.0;
    long double scanned_long_double = 0.0L;
    char float_tail = 0;
    if (scan_with_va_list("1.25 -2.5e1 3.125!", "%f %lE %LG%c",
                          &scanned_float, &scanned_double,
                          &scanned_long_double, &float_tail) != 4) return 54;
    if ((int)(scanned_float * 100.0f) != 125 ||
        (int)scanned_double != -25 ||
        (int)(scanned_long_double * 1000.0L) != 3125 ||
        float_tail != '!') return 55;

    scanned_float = 0.0f;
    float_tail = 0;
    if (sscanf("12.34x", "%4f%c", &scanned_float, &float_tail) != 2) return 56;
    if ((int)(scanned_float * 10.0f) != 123 || float_tail != '4') return 57;

    scanned_double = 0.0;
    if (sscanf("9.5 7.25", "%*g %lf", &scanned_double) != 1) return 58;
    if ((int)(scanned_double * 100.0) != 725) return 59;

    scanned_float = 91.0f;
    if (sscanf("", "%f", &scanned_float) != EOF) return 60;
    if ((int)scanned_float != 91) return 61;
    if (sscanf("x", "%f", &scanned_float) != 0) return 62;
    if ((int)scanned_float != 91) return 63;

    scanned_float = 0.0f;
    scanned_double = 0.0;
    if (sscanf("INF -infinity", "%f %lf",
               &scanned_float, &scanned_double) != 2) return 64;
    if (!(scanned_float > 1.0e30f) ||
        !(scanned_double < -1.0e300)) return 65;

    scanned_double = 0.0;
    float_tail = 0;
    if (sscanf("nan(payload)!", "%lf%c",
               &scanned_double, &float_tail) != 2) return 66;
    if (scanned_double == scanned_double || float_tail != '!') return 67;

    scanned_double = 0.0;
    float_tail = 0;
    if (sscanf("infinity", "%3lf%c",
               &scanned_double, &float_tail) != 2) return 68;
    if (!(scanned_double > 1.0e300) || float_tail != 'i') return 69;

    double scanned_hex_float = 0.0;
    float_tail = 0;
    if (sscanf("0x1.8p+3!", "%la%c",
               &scanned_hex_float, &float_tail) != 2) return 70;
    if ((int)scanned_hex_float != 12 || float_tail != '!') return 71;

    float scanned_hex_small = 0.0f;
    if (sscanf("0X1P-2", "%A", &scanned_hex_small) != 1) return 72;
    if ((int)(scanned_hex_small * 100.0f) != 25) return 73;

    scanned_hex_float = 0.0;
    float_tail = 0;
    if (sscanf("0x1.8p+3Z", "%8la%c",
               &scanned_hex_float, &float_tail) != 2) return 74;
    if ((int)scanned_hex_float != 12 || float_tail != 'Z') return 75;

    signed char hhd_slots[4] = {11, 0, 22, 33};
    unsigned char hhu_slots[4] = {44, 0, 55, 66};
    if (sscanf("-101 250", "%hhd %hhu",
               &hhd_slots[1], &hhu_slots[1]) != 2) return 76;
    if (hhd_slots[0] != 11 || hhd_slots[1] != -101 ||
        hhd_slots[2] != 22 || hhd_slots[3] != 33 ||
        hhu_slots[0] != 44 || hhu_slots[1] != 250 ||
        hhu_slots[2] != 55 || hhu_slots[3] != 66) return 77;

    short hd_slots[3] = {111, 0, 222};
    unsigned short hu_slots[3] = {333, 0, 444};
    if (sscanf("-30000 60000", "%hd %hu",
               &hd_slots[1], &hu_slots[1]) != 2) return 78;
    if (hd_slots[0] != 111 || hd_slots[1] != -30000 || hd_slots[2] != 222 ||
        hu_slots[0] != 333 || hu_slots[1] != 60000 ||
        hu_slots[2] != 444) return 79;

    char hhn_chars[3] = {0, 0, 0};
    signed char hhn_slots[4] = {7, -1, 8, 9};
    if (sscanf("ABC", "%3c%hhn",
               hhn_chars, &hhn_slots[1]) != 1) return 80;
    if (hhn_chars[0] != 'A' || hhn_chars[1] != 'B' || hhn_chars[2] != 'C' ||
        hhn_slots[0] != 7 || hhn_slots[1] != 3 ||
        hhn_slots[2] != 8 || hhn_slots[3] != 9) return 81;

    char hn_chars[3] = {0, 0, 0};
    short hn_slots[3] = {123, -1, 456};
    if (sscanf("XYZ", "%3c%hn",
               hn_chars, &hn_slots[1]) != 1) return 82;
    if (hn_chars[0] != 'X' || hn_chars[1] != 'Y' || hn_chars[2] != 'Z' ||
        hn_slots[0] != 123 || hn_slots[1] != 3 ||
        hn_slots[2] != 456) return 83;

    intmax_t scanned_intmax = 0;
    uintmax_t scanned_uintmax = 0;
    if (sscanf("-5000000000123 18000000000000000000", "%jd %ju",
               &scanned_intmax, &scanned_uintmax) != 2) return 84;
    if (scanned_intmax != -5000000000123LL ||
        scanned_uintmax != 18000000000000000000ULL) return 85;

    size_t scanned_size = 0;
    ptrdiff_t scanned_ptrdiff = 0;
    if (sscanf("5000000000 -4000000000", "%zu %td",
               &scanned_size, &scanned_ptrdiff) != 2) return 86;
    if (scanned_size != 5000000000UL ||
        scanned_ptrdiff != -4000000000L) return 87;

    char j_count_chars[4] = {0, 0, 0, 0};
    intmax_t j_count = -1;
    if (sscanf("ABCD", "%4c%jn",
               j_count_chars, &j_count) != 1) return 88;
    if (j_count_chars[0] != 'A' || j_count_chars[3] != 'D' ||
        j_count != 4) return 89;

    long z_count = -1;
    ptrdiff_t t_count = -1;
    if (sscanf("abc", "%*3c%zn", &z_count) != 0) return 90;
    if (sscanf("xy", "%*2c%tn", &t_count) != 0) return 91;
    if (z_count != 3 || t_count != 2) return 92;

    return 0;
}
