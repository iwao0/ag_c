#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
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
    wchar_t buffer[96];

    if (swprintf(buffer, 96, L"%hhd/%hhu", 255, 258) != 4) return 1;
    if (wcscmp(buffer, L"-1/2") != 0) return 2;

    if (swprintf(buffer, 96, L"%hd/%hu", 65535, 65538) != 4) return 3;
    if (wcscmp(buffer, L"-1/2") != 0) return 4;

    intmax_t intmax_value = -5000000000123LL;
    uintmax_t uintmax_value = 18000000000000000000ULL;
    if (swprintf(buffer, 96, L"%jd/%ju",
                 intmax_value, uintmax_value) != 35) return 5;
    if (wcscmp(buffer, L"-5000000000123/18000000000000000000") != 0) return 6;

    size_t size_value = 5000000000UL;
    ptrdiff_t difference_value = -4000000000L;
    if (swprintf(buffer, 96, L"%zu/%td",
                 size_value, difference_value) != 22) return 7;
    if (wcscmp(buffer, L"5000000000/-4000000000") != 0) return 8;

    signed char hhn_slots[4] = {7, -1, 8, 9};
    if (swprintf(buffer, 96, L"abc%hhn", &hhn_slots[1]) != 3) return 9;
    if (wcscmp(buffer, L"abc") != 0 ||
        hhn_slots[0] != 7 || hhn_slots[1] != 3 ||
        hhn_slots[2] != 8 || hhn_slots[3] != 9) return 10;

    short hn_slots[3] = {123, -1, 456};
    if (swprintf(buffer, 96, L"wxyz%hn", &hn_slots[1]) != 4) return 11;
    if (wcscmp(buffer, L"wxyz") != 0 ||
        hn_slots[0] != 123 || hn_slots[1] != 4 ||
        hn_slots[2] != 456) return 12;

    intmax_t j_count = -1;
    if (swprintf(buffer, 96, L"AB%jn", &j_count) != 2) return 13;
    if (wcscmp(buffer, L"AB") != 0 || j_count != 2) return 14;

    long z_count = -1;
    ptrdiff_t t_count = -1;
    if (swprintf(buffer, 96, L"A%znBC%tnD",
                 &z_count, &t_count) != 4) return 15;
    if (wcscmp(buffer, L"ABCD") != 0 ||
        z_count != 1 || t_count != 3) return 16;

    if (format_with_va_list(buffer, 96, L"%hhd:%hu:%jd:%zu:%td",
                            255, 65538, intmax_value,
                            size_value, difference_value) != 42) return 17;
    if (wcscmp(buffer,
               L"-1:2:-5000000000123:5000000000:-4000000000") != 0) return 18;

    j_count = -1;
    z_count = -1;
    t_count = -1;
    if (format_with_va_list(buffer, 96, L"AB%jnCD%znEF%tn",
                            &j_count, &z_count, &t_count) != 6) return 19;
    if (wcscmp(buffer, L"ABCDEF") != 0 ||
        j_count != 2 || z_count != 4 || t_count != 6) return 20;

    if (swprintf(buffer, 96, L"%8.5u/%+.5d", 42, -7) != 15) return 21;
    if (wcscmp(buffer, L"   00042/-00007") != 0) return 22;

    if (swprintf(buffer, 96, L"%+6.3d/% 6.3i", 7, 7) != 13) return 23;
    if (wcscmp(buffer, L"  +007/   007") != 0) return 24;

    if (swprintf(buffer, 96, L"%.0u", 0) != 0) return 25;
    if (buffer[0] != L'\0') return 26;

    return 0;
}
