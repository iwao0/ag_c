/*
 * Address-taken variadic libc functions use their declared fixed-parameter
 * function type.  Extra arguments travel through the Wasm va_arg area.
 * Expected: exit=0
 */
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

static int (*printf_signature)(const char *, ...) = printf;
static int (*fprintf_signature)(FILE *, const char *, ...) = fprintf;
static int (*snprintf_signature)(
    char *, size_t, const char *, ...) = snprintf;
static int (*sscanf_signature)(const char *, const char *, ...) = sscanf;
static int (*swprintf_signature)(
    wchar_t *, size_t, const wchar_t *, ...) = swprintf;
static int (*swscanf_signature)(
    const wchar_t *, const wchar_t *, ...) = swscanf;

int main(void) {
    char narrow[16] = {0};
    wchar_t wide[16] = {0};
    int first = 0;
    int second = 0;

    if (printf_signature("") != 0) return 1;
    if (fprintf_signature(stdout, "") != 0) return 2;

    if (snprintf_signature(narrow, sizeof(narrow), "%s/%u", "cd", 5u) != 4 ||
        narrow[0] != 'c' || narrow[1] != 'd' ||
        narrow[2] != '/' || narrow[3] != '5' || narrow[4] != '\0') {
        return 3;
    }
    if (sscanf_signature("6 7", "%d %d", &first, &second) != 2 ||
        first != 6 || second != 7) {
        return 4;
    }

    if (swprintf_signature(
            wide, sizeof(wide) / sizeof(wide[0]),
            L"%ls:%d", L"ef", 8) != 4 ||
        wide[0] != L'e' || wide[1] != L'f' ||
        wide[2] != L':' || wide[3] != L'8' || wide[4] != L'\0') {
        return 5;
    }
    first = 0;
    second = 0;
    if (swscanf_signature(L"9 10", L"%d %d", &first, &second) != 2 ||
        first != 9 || second != 10) {
        return 6;
    }
    return 0;
}
