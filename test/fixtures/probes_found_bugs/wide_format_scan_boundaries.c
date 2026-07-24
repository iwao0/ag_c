#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

int main(void) {
    wchar_t wide_word[8];
    wchar_t wide_ch = 0;
    char narrow_word[8];
    char narrow_ch = 0;

    if (swscanf(L"wide Z", L"%ls %lc", wide_word, &wide_ch) != 2) return 1;
    if (wcscmp(wide_word, L"wide") != 0 || wide_ch != L'Z') return 2;
    if (swscanf(L"byte Q", L"%s %c", narrow_word, &narrow_ch) != 2) return 3;
    if (strcmp(narrow_word, "byte") != 0 || narrow_ch != 'Q') return 4;

    char limited_word[4] = {'x', 'x', 'x', 'x'};
    char following_ch = 0;
    if (swscanf(L"abcdef", L"%3s%c", limited_word, &following_ch) != 2) return 24;
    if (strcmp(limited_word, "abc") != 0 || following_ch != 'd') return 25;

    wchar_t limited_wide[4] = {L'x', L'x', L'x', L'x'};
    wchar_t following_wide = 0;
    if (swscanf(L"uvwxyz", L"%3ls%lc", limited_wide, &following_wide) != 2) return 26;
    if (wcscmp(limited_wide, L"uvw") != 0 || following_wide != L'x') return 27;

    int after_suppressed = 0;
    if (swscanf(L"11 22", L"%*d %d", &after_suppressed) != 1) return 28;
    if (after_suppressed != 22) return 29;

    int limited_integer = 0;
    char integer_tail = 0;
    if (swscanf(L"12345", L"%3d%c", &limited_integer, &integer_tail) != 2) return 30;
    if (limited_integer != 123 || integer_tail != '4') return 31;

    char two_chars[2] = {0, 0};
    char char_tail = 0;
    if (swscanf(L"ABCDE", L"%2c%c", two_chars, &char_tail) != 2) return 32;
    if (two_chars[0] != 'A' || two_chars[1] != 'B' || char_tail != 'C') return 33;

    int untouched_scan = 73;
    if (swscanf(L"", L"%d", &untouched_scan) != EOF) return 34;
    if (untouched_scan != 73) return 35;
    if (swscanf(L"x", L"%d", &untouched_scan) != 0) return 36;
    if (untouched_scan != 73) return 37;
    if (swscanf(L"12", L"%*d") != 0) return 38;
    if (swscanf(L"12", L"%*d%d", &untouched_scan) != EOF) return 39;
    if (untouched_scan != 73) return 40;

    int consumed = -1;
    char counted_chars[2] = {0, 0};
    if (swscanf(L"ABZ", L"%2c%n", counted_chars, &consumed) != 1) return 41;
    if (counted_chars[0] != 'A' || counted_chars[1] != 'B' ||
        consumed != 2) return 42;

    long consumed_long = -1;
    wchar_t counted_wide[2] = {0, 0};
    if (swscanf(L"xyZ", L"%2lc%ln", counted_wide, &consumed_long) != 1) return 43;
    if (counted_wide[0] != L'x' || counted_wide[1] != L'y' ||
        consumed_long != 2) return 44;

    unsigned scanned_hex = 0;
    unsigned scanned_octal = 0;
    if (swscanf(L"0X2a 075", L"%X %o",
                &scanned_hex, &scanned_octal) != 2) return 45;
    if (scanned_hex != 42u || scanned_octal != 61u) return 46;

    int scanned_auto_hex = 0;
    int scanned_auto_octal = 0;
    if (swscanf(L"-0x2A 077", L"%i %i",
                &scanned_auto_hex, &scanned_auto_octal) != 2) return 47;
    if (scanned_auto_hex != -42 || scanned_auto_octal != 63) return 48;

    long scanned_auto_long = 0;
    if (swscanf(L"0x100000000", L"%li", &scanned_auto_long) != 1) return 49;
    if (scanned_auto_long != 0x100000000L) return 50;

    int width_auto = 0;
    char width_tail = 0;
    if (swscanf(L"0x2fZ", L"%4i%c", &width_auto, &width_tail) != 2) return 51;
    if (width_auto != 47 || width_tail != 'Z') return 52;

    void *scanned_pointer = 0;
    char pointer_tail = 0;
    if (swscanf(L"0X2aZ", L"%p%c",
                &scanned_pointer, &pointer_tail) != 2) return 53;
    if (scanned_pointer != (void *)42 || pointer_tail != 'Z') return 54;

    scanned_pointer = 0;
    pointer_tail = 0;
    if (swscanf(L"0x2fQ", L"%4p%c",
                &scanned_pointer, &pointer_tail) != 2) return 55;
    if (scanned_pointer != (void *)47 || pointer_tail != 'Q') return 56;

    char scan_letters[4] = {0, 0, 0, 0};
    char scan_digits[4] = {0, 0, 0, 0};
    if (swscanf(L"abc123Z", L"%3[abc]%3[^Z]",
                scan_letters, scan_digits) != 2) return 57;
    if (strcmp(scan_letters, "abc") != 0 ||
        strcmp(scan_digits, "123") != 0) return 58;

    char scan_tail = 0;
    memset(scan_digits, 0, sizeof(scan_digits));
    if (swscanf(L"skip789Q", L"%*[skip]%3[0123456789]%c",
                scan_digits, &scan_tail) != 2) return 59;
    if (strcmp(scan_digits, "789") != 0 || scan_tail != 'Q') return 60;

    wchar_t scan_wide[4] = {0, 0, 0, 0};
    wchar_t wide_scan_tail = 0;
    if (swscanf(L"xyzR", L"%3l[xyz]%lc",
                scan_wide, &wide_scan_tail) != 2) return 61;
    if (wcscmp(scan_wide, L"xyz") != 0 || wide_scan_tail != L'R') return 62;

    char scan_bracket[3] = {0, 0, 0};
    scan_tail = 0;
    if (swscanf(L"]-X", L"%2[]-]%c",
                scan_bracket, &scan_tail) != 2) return 63;
    if (strcmp(scan_bracket, "]-") != 0 || scan_tail != 'X') return 64;

    scan_letters[0] = 'q';
    if (swscanf(L"", L"%[abc]", scan_letters) != EOF) return 65;
    if (scan_letters[0] != 'q') return 66;
    if (swscanf(L"7", L"%[abc]", scan_letters) != 0) return 67;
    if (scan_letters[0] != 'q') return 68;

    float scanned_float = 0.0f;
    double scanned_double = 0.0;
    if (swscanf(L"1.25 -2.5e1", L"%f %lE",
                &scanned_float, &scanned_double) != 2) return 69;
    if ((int)(scanned_float * 100.0f) != 125 ||
        (int)scanned_double != -25) return 70;

    long double scanned_long_double = 0.0L;
    char float_tail = 0;
    if (swscanf(L"3.125!", L"%LG%c",
                &scanned_long_double, &float_tail) != 2) return 71;
    if ((int)(scanned_long_double * 1000.0L) != 3125 ||
        float_tail != '!') return 72;

    scanned_float = 0.0f;
    float_tail = 0;
    if (swscanf(L"12.34x", L"%4f%c",
                &scanned_float, &float_tail) != 2) return 73;
    if ((int)(scanned_float * 10.0f) != 123 || float_tail != '4') return 74;

    scanned_double = 0.0;
    if (swscanf(L"9.5 7.25", L"%*g %lf", &scanned_double) != 1) return 75;
    if ((int)(scanned_double * 100.0) != 725) return 76;

    scanned_float = 91.0f;
    if (swscanf(L"", L"%f", &scanned_float) != EOF) return 77;
    if ((int)scanned_float != 91) return 78;
    if (swscanf(L"x", L"%f", &scanned_float) != 0) return 79;
    if ((int)scanned_float != 91) return 80;

    scanned_float = 0.0f;
    scanned_double = 0.0;
    if (swscanf(L"INF -infinity", L"%f %lf",
                &scanned_float, &scanned_double) != 2) return 81;
    if (!(scanned_float > 1.0e30f) ||
        !(scanned_double < -1.0e300)) return 82;

    scanned_double = 0.0;
    float_tail = 0;
    if (swscanf(L"nan(payload)!", L"%lf%c",
                &scanned_double, &float_tail) != 2) return 83;
    if (scanned_double == scanned_double || float_tail != '!') return 84;

    scanned_double = 0.0;
    float_tail = 0;
    if (swscanf(L"infinity", L"%3lf%c",
                &scanned_double, &float_tail) != 2) return 85;
    if (!(scanned_double > 1.0e300) || float_tail != 'i') return 86;

    double scanned_hex_float = 0.0;
    float_tail = 0;
    if (swscanf(L"0x1.8p+3!", L"%la%c",
                &scanned_hex_float, &float_tail) != 2) return 87;
    if ((int)scanned_hex_float != 12 || float_tail != '!') return 88;

    float scanned_hex_small = 0.0f;
    if (swscanf(L"0X1P-2", L"%A", &scanned_hex_small) != 1) return 89;
    if ((int)(scanned_hex_small * 100.0f) != 25) return 90;

    scanned_hex_float = 0.0;
    float_tail = 0;
    if (swscanf(L"0x1.8p+3Z", L"%8la%c",
                &scanned_hex_float, &float_tail) != 2) return 91;
    if ((int)scanned_hex_float != 12 || float_tail != 'Z') return 92;

    signed char hhd_slots[4] = {11, 0, 22, 33};
    unsigned char hhu_slots[4] = {44, 0, 55, 66};
    if (swscanf(L"-101 250", L"%hhd %hhu",
                &hhd_slots[1], &hhu_slots[1]) != 2) return 93;
    if (hhd_slots[0] != 11 || hhd_slots[1] != -101 ||
        hhd_slots[2] != 22 || hhd_slots[3] != 33 ||
        hhu_slots[0] != 44 || hhu_slots[1] != 250 ||
        hhu_slots[2] != 55 || hhu_slots[3] != 66) return 94;

    short hd_slots[3] = {111, 0, 222};
    unsigned short hu_slots[3] = {333, 0, 444};
    if (swscanf(L"-30000 60000", L"%hd %hu",
                &hd_slots[1], &hu_slots[1]) != 2) return 95;
    if (hd_slots[0] != 111 || hd_slots[1] != -30000 || hd_slots[2] != 222 ||
        hu_slots[0] != 333 || hu_slots[1] != 60000 ||
        hu_slots[2] != 444) return 96;

    char hhn_chars[3] = {0, 0, 0};
    signed char hhn_slots[4] = {7, -1, 8, 9};
    if (swscanf(L"ABC", L"%3c%hhn",
                hhn_chars, &hhn_slots[1]) != 1) return 97;
    if (hhn_chars[0] != 'A' || hhn_chars[1] != 'B' || hhn_chars[2] != 'C' ||
        hhn_slots[0] != 7 || hhn_slots[1] != 3 ||
        hhn_slots[2] != 8 || hhn_slots[3] != 9) return 98;

    char hn_chars[3] = {0, 0, 0};
    short hn_slots[3] = {123, -1, 456};
    if (swscanf(L"XYZ", L"%3c%hn",
                hn_chars, &hn_slots[1]) != 1) return 99;
    if (hn_chars[0] != 'X' || hn_chars[1] != 'Y' || hn_chars[2] != 'Z' ||
        hn_slots[0] != 123 || hn_slots[1] != 3 ||
        hn_slots[2] != 456) return 100;

    intmax_t scanned_intmax = 0;
    uintmax_t scanned_uintmax = 0;
    if (swscanf(L"-5000000000123 18000000000000000000", L"%jd %ju",
                &scanned_intmax, &scanned_uintmax) != 2) return 101;
    if (scanned_intmax != -5000000000123LL ||
        scanned_uintmax != 18000000000000000000ULL) return 102;

    size_t scanned_size = 0;
    ptrdiff_t scanned_ptrdiff = 0;
    if (swscanf(L"5000000000 -4000000000", L"%zu %td",
                &scanned_size, &scanned_ptrdiff) != 2) return 103;
    if (scanned_size != 5000000000UL ||
        scanned_ptrdiff != -4000000000L) return 104;

    char j_count_chars[4] = {0, 0, 0, 0};
    intmax_t j_count = -1;
    if (swscanf(L"ABCD", L"%4c%jn",
                j_count_chars, &j_count) != 1) return 105;
    if (j_count_chars[0] != 'A' || j_count_chars[3] != 'D' ||
        j_count != 4) return 106;

    long z_count = -1;
    ptrdiff_t t_count = -1;
    if (swscanf(L"abc", L"%*3c%zn", &z_count) != 0) return 107;
    if (swscanf(L"xy", L"%*2c%tn", &t_count) != 0) return 108;
    if (z_count != 3 || t_count != 2) return 109;

    long scanned_long = 0;
    unsigned long scanned_ulong = 0;
    if (swscanf(L"-1234567890123 4000000000", L"%ld %lu",
                &scanned_long, &scanned_ulong) != 2) return 14;
    if (scanned_long != -1234567890123L ||
        scanned_ulong != 4000000000UL) return 15;

    long long scanned_llong = 0;
    unsigned long long scanned_ullong = 0;
    if (swscanf(L"-5000000000123 18000000000000000000", L"%lld %llu",
                &scanned_llong, &scanned_ullong) != 2) return 16;
    if (scanned_llong != -5000000000123LL ||
        scanned_ullong != 18000000000000000000ULL) return 17;

    wchar_t padded[16];
    if (swprintf(padded, 16, L"[%02d]", 7) != 4) return 5;
    if (wcscmp(padded, L"[07]") != 0) return 6;

    wchar_t field[16];
    if (swprintf(field, 16, L"[%5d]", 42) != 7) return 18;
    if (wcscmp(field, L"[   42]") != 0) return 19;
    if (swprintf(field, 16, L"[%-5u]", 7u) != 7) return 20;
    if (wcscmp(field, L"[7    ]") != 0) return 21;
    if (swprintf(field, 16, L"[%05d]", -42) != 7) return 22;
    if (wcscmp(field, L"[-0042]") != 0) return 23;

    wchar_t small[4] = {L'x', L'x', L'x', L'x'};
    if (swprintf(small, 4, L"%d", 12345) >= 0) return 7;

    wchar_t untouched[2] = {L'q', L'r'};
    if (swprintf(untouched, 0, L"%d-%d", 5, 6) >= 0) return 8;
    if (untouched[0] != L'q' || untouched[1] != L'r') return 9;

    wchar_t long_values[64];
    if (swprintf(long_values, 64, L"%ld/%lu",
                 -1234567890123L, 4000000000UL) != 25) return 10;
    if (wcscmp(long_values, L"-1234567890123/4000000000") != 0) return 11;

    wchar_t long_long_values[80];
    if (swprintf(long_long_values, 80, L"%lld/%llu",
                 -5000000000123LL, 18000000000000000000ULL) != 35) return 12;
    if (wcscmp(long_long_values,
               L"-5000000000123/18000000000000000000") != 0) return 13;
    return 0;
}
