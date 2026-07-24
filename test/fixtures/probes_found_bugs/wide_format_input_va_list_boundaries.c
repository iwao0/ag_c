#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

static int scan_with_va_list(const wchar_t *input, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vswscanf(input, format, args);
    va_end(args);
    return result;
}

int main(void) {
    signed char narrow = 0;
    unsigned short short_unsigned = 0;
    intmax_t maximum = 0;
    size_t size = 0;
    ptrdiff_t difference = 0;

    if (scan_with_va_list(
            L"-1 65535 -5000000000123 5000000000 -4000000000",
            L"%hhd %hu %jd %zu %td",
            &narrow, &short_unsigned, &maximum, &size, &difference) != 5) return 1;
    if (narrow != -1 || short_unsigned != 65535 ||
        maximum != -5000000000123LL ||
        size != 5000000000UL || difference != -4000000000L) return 2;

    int first = 0;
    int second = 0;
    long count = -1;
    if (scan_with_va_list(L"11 22 33", L"%*d %d %zn%d",
                          &first, &count, &second) != 2) return 3;
    if (first != 22 || count != 6 || second != 33) return 4;

    return 0;
}
