/*
 * Formatted-output size limits are size_t values.  A short result must fit
 * normally even when the caller supplies SIZE_MAX as the available capacity.
 * Expected: exit=0
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#ifdef __wasm32__
#define CALL_SNPRINTF snprintf
#define CALL_SWPRINTF swprintf
#else
static int (*snprintf_signature)(
    char *, size_t, const char *, ...) = snprintf;
static int (*swprintf_signature)(
    wchar_t *, size_t, const wchar_t *, ...) = swprintf;
#define CALL_SNPRINTF snprintf_signature
#define CALL_SWPRINTF swprintf_signature
#endif

static int (*vsnprintf_signature)(
    char *, size_t, const char *, va_list) = vsnprintf;
static int (*vswprintf_signature)(
    wchar_t *, size_t, const wchar_t *, va_list) = vswprintf;

static int call_vsnprintf(
    char *buffer, size_t size, const char *format, ...) {
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vsnprintf_signature(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

static int call_vswprintf(
    wchar_t *buffer, size_t size, const wchar_t *format, ...) {
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = vswprintf_signature(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

int main(void) {
    size_t maximum = (size_t)-1;
    char narrow[16] = {
        '?', '?', '?', '?', '?', '?', '?', '?',
        '?', '?', '?', '?', '?', '?', '?', '?'
    };
    wchar_t wide[16] = {
        L'?', L'?', L'?', L'?', L'?', L'?', L'?', L'?',
        L'?', L'?', L'?', L'?', L'?', L'?', L'?', L'?'
    };

    if (CALL_SNPRINTF(narrow, maximum, "%s:%d", "ab", 7) != 4 ||
        narrow[0] != 'a' || narrow[1] != 'b' || narrow[2] != ':' ||
        narrow[3] != '7' || narrow[4] != '\0' || narrow[5] != '?') {
        return 1;
    }
    if (call_vsnprintf(narrow, maximum, "%c/%u", 'X', 9u) != 3 ||
        narrow[0] != 'X' || narrow[1] != '/' || narrow[2] != '9' ||
        narrow[3] != '\0' || narrow[4] != '\0') {
        return 2;
    }

    if (CALL_SWPRINTF(wide, maximum, L"%ls:%d", L"cd", 8) != 4 ||
        wide[0] != L'c' || wide[1] != L'd' || wide[2] != L':' ||
        wide[3] != L'8' || wide[4] != L'\0' || wide[5] != L'?') {
        return 3;
    }
    if (call_vswprintf(wide, maximum, L"%lc/%u", L'Y', 6u) != 3 ||
        wide[0] != L'Y' || wide[1] != L'/' || wide[2] != L'6' ||
        wide[3] != L'\0' || wide[4] != L'\0') {
        return 4;
    }
    return 0;
}
