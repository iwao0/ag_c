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
    wchar_t buffer[128];

    if (swprintf(buffer, 128, L"%#x/%#X", 0x2a, 0x2a) != 9) return 1;
    if (wcscmp(buffer, L"0x2a/0X2A") != 0) return 2;

    if (swprintf(buffer, 128, L"%#o", 052) != 3) return 3;
    if (wcscmp(buffer, L"052") != 0) return 4;

    if (swprintf(buffer, 128, L"%08x/%-8o", 0x2a, 052) != 17) return 5;
    if (wcscmp(buffer, L"0000002a/52      ") != 0) return 6;

    if (swprintf(buffer, 128, L"%.5x/%#.5o", 0x2a, 052) != 11) return 7;
    if (wcscmp(buffer, L"0002a/00052") != 0) return 8;

    if (swprintf(buffer, 128, L"%08.5x", 0x2a) != 8) return 9;
    if (wcscmp(buffer, L"   0002a") != 0) return 10;

    uintmax_t wide_value = UINT64_C(0x123456789abcdef0);
    size_t size_value = 012345670UL;
    if (format_with_va_list(buffer, 128, L"%hhx:%hX:%jx:%zo",
                            0x1ff, 0x12345, wide_value, size_value) != 33) return 11;
    if (wcscmp(buffer, L"ff:2345:123456789abcdef0:12345670") != 0) return 12;

    void *pointer_value = (void *)(uintptr_t)0x1234;
    if (swprintf(buffer, 128, L"%10p", pointer_value) != 10) return 13;
    if (wcscmp(buffer, L"    0x1234") != 0) return 14;

    if (format_with_va_list(buffer, 128, L"%-10p/%#jx",
                            pointer_value, wide_value) != 29) return 15;
    if (wcscmp(buffer, L"0x1234    /0x123456789abcdef0") != 0) return 16;

    if (swprintf(buffer, 128, L"%#08x", 0x2a) != 8) return 17;
    if (wcscmp(buffer, L"0x00002a") != 0) return 18;

    if (swprintf(buffer, 128, L"%#.0x/%#.0o", 0, 0) != 2) return 19;
    if (wcscmp(buffer, L"/0") != 0) return 20;

    if (format_with_va_list(buffer, 128, L"%#*.*x", 10, 5, 0x2a) != 10) return 21;
    if (wcscmp(buffer, L"   0x0002a") != 0) return 22;

    return 0;
}
