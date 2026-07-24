#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

static int format_to_stream(FILE *stream, const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vfwprintf(stream, format, args);
    va_end(args);
    return result;
}

static int format_to_stdout(const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vwprintf(format, args);
    va_end(args);
    return result;
}

int main(void) {
    FILE *stream = tmpfile();
    int stream_is_file = stream != 0;
    if (!stream) stream = stdout;
    if (!stream) stream = (FILE *)1;
    if (stream_is_file && fputwc(L'X', stream) != L'X') return 12;

    int direct_count = -1;
    int direct_result = fwprintf(stream, L"A%hhd%n", 255, &direct_count);
    if (direct_result < 0) return 1;
    if (direct_result != 3) return 2;
    if (direct_count != 3) return 3;

    if (fwprintf(stream, L"E%4.2s/%-3lc", "abc", L'Q') != 9) return 13;

    int va_count = -1;
    intmax_t intmax_value = -5000000000123LL;
    size_t size_value = 5000000000UL;
    ptrdiff_t difference_value = -4000000000L;
    if (format_to_stream(stream, L"C%jd/%zu/%td%n",
                         intmax_value, size_value, difference_value,
                         &va_count) != 38) return 4;
    if (va_count != 38) return 5;

    if (format_to_stream(stream, L"F%-5.3ls", L"HELLO") != 6) return 14;

    if (fwprintf(stream, L"G%+.1f", 1.5) != 5) return 15;
    if (format_to_stream(stream, L"H%.2e", 0.125) != 9) return 16;

    if (stream_is_file) {
        wchar_t contents[96];
        rewind(stream);
        if (!fgetws(contents, 96, stream)) return 6;
        if (wcscmp(contents,
                   L"XA-1E  ab/Q  C-5000000000123/5000000000/-4000000000FHEL  "
                   L"G+1.5H1.25e-01") != 0) {
            return 7;
        }
        if (fclose(stream) != 0) return 8;
    }

    if (wprintf(L"B%hhu", 258) != 2) return 9;
    int stdout_count = -1;
    if (format_to_stdout(L"D%hu%n", 65538, &stdout_count) != 2) return 10;
    if (stdout_count != 2) return 11;
    return 0;
}
