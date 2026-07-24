#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

static int scan_from_stream(FILE *stream, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfwscanf(stream, format, args);
    va_end(args);
    return result;
}

static int scan_from_stdin(const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vwscanf(format, args);
    va_end(args);
    return result;
}

int main(void) {
    if (wscanf(L"") != 0) return 1;
    if (scan_from_stdin(L"") != 0) return 2;

    FILE *stream = tmpfile();
    if (!stream) {
        signed char narrow = -1;
        unsigned short small = 0;
        wchar_t word[8] = {0};
        if (fwscanf((FILE *)1, L"%hhd %hu %ls",
                    &narrow, &small, word) != WEOF) return 3;
        if (scan_from_stream((FILE *)1, L"%jd %zu",
                             (intmax_t *)0, (size_t *)0) != WEOF) return 4;
        return 0;
    }

    if (fputws(L"17 23 tail\n-5000000000123 5000000000", stream) < 0) return 5;
    rewind(stream);

    signed char narrow = -1;
    unsigned short small = 0;
    wchar_t word[8] = {0};
    int direct_count = -1;
    if (fwscanf(stream, L"%hhd %hu %ls%n",
                &narrow, &small, word, &direct_count) != 3) return 6;
    if (narrow != 17 || small != 23 ||
        wcscmp(word, L"tail") != 0 || direct_count != 10) return 7;

    intmax_t large = 0;
    size_t size_value = 0;
    int va_count = -1;
    if (scan_from_stream(stream, L" %jd %zu%n",
                         &large, &size_value, &va_count) != 2) return 8;
    if (large != -5000000000123LL ||
        size_value != 5000000000UL || va_count != 26) return 9;
    if (fgetwc(stream) != WEOF) return 10;
    if (fclose(stream) != 0) return 11;
    return 0;
}
