#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

    if (snprintf(buffer, sizeof(buffer), "[%8.3s]", "abcdef") != 10) return 1;
    if (strcmp(buffer, "[     abc]") != 0) return 2;

    if (snprintf(buffer, sizeof(buffer), "[%-7.4s]", "wxyz12") != 9) return 3;
    if (strcmp(buffer, "[wxyz   ]") != 0) return 4;

    if (snprintf(buffer, sizeof(buffer), "%5c/%-5c", 'A', 'Z') != 11) return 5;
    if (strcmp(buffer, "    A/Z    ") != 0) return 6;

    if (format_with_va_list(buffer, sizeof(buffer), "%*.*s|%*.*s",
                            -6, 3, "abcdef", 7, 2, "WXYZ") != 14) return 7;
    if (strcmp(buffer, "abc   |     WX") != 0) return 8;

    if (snprintf(buffer, sizeof(buffer), "%.0s/%.0s", "abc", "XYZ") != 1) return 9;
    if (strcmp(buffer, "/") != 0) return 10;

    char small[5];
    if (snprintf(small, sizeof(small), "%8s", "ab") != 8) return 11;
    if (strcmp(small, "    ") != 0) return 12;

    return 0;
}
