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
    wchar_t buffer[96];

    if (swprintf(buffer, 96, L"[%8.3s]", "abcdef") != 10) return 1;
    if (wcscmp(buffer, L"[     abc]") != 0) return 2;

    if (swprintf(buffer, 96, L"[%-7.4ls]", L"wxyz12") != 9) return 3;
    if (wcscmp(buffer, L"[wxyz   ]") != 0) return 4;

    if (swprintf(buffer, 96, L"%5c/%-5lc", 'A', L'Z') != 11) return 5;
    if (wcscmp(buffer, L"    A/Z    ") != 0) return 6;

    if (format_with_va_list(buffer, 96, L"%*.*s|%*.*ls",
                            -6, 3, "abcdef", 7, 2, L"WXYZ") != 14) return 7;
    if (wcscmp(buffer, L"abc   |     WX") != 0) return 8;

    if (swprintf(buffer, 96, L"%.0s/%.0ls", "abc", L"XYZ") != 1) return 9;
    if (wcscmp(buffer, L"/") != 0) return 10;

    return 0;
}
