#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int format_with_va_list(char *buffer, size_t size,
                               const char *format, ...) {
    va_list args;
    va_start(args, format);
    int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

int main(void) {
    char buffer[96];

    if (snprintf(buffer, sizeof(buffer), "[%8.3ls]", L"abcdef") != 10) return 1;
    if (strcmp(buffer, "[     abc]") != 0) return 2;

    if (snprintf(buffer, sizeof(buffer), "[%-7.4ls]", L"wxyz12") != 9) return 3;
    if (strcmp(buffer, "[wxyz   ]") != 0) return 4;

    if (snprintf(buffer, sizeof(buffer), "%5lc/%-5lc", L'A', L'Z') != 11) return 5;
    if (strcmp(buffer, "    A/Z    ") != 0) return 6;

    if (format_with_va_list(buffer, sizeof(buffer), "%*.*ls|%*lc",
                            -6, 3, L"abcdef", 5, L'Q') != 12) return 7;
    if (strcmp(buffer, "abc   |    Q") != 0) return 8;

    if (snprintf(buffer, sizeof(buffer), "%.0ls", L"XYZ") != 0) return 9;
    if (strcmp(buffer, "") != 0) return 10;

    return 0;
}
