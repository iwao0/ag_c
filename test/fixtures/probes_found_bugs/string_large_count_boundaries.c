/*
 * NUL-terminated string operations must not truncate or sign-interpret their
 * 64-bit size_t limits before they encounter the short source/string end.
 * Expected: exit=0
 */
#include <stddef.h>
#include <string.h>
#include <wchar.h>

static int (*strncmp_signature)(const char *, const char *, size_t) = strncmp;
static char *(*strncat_signature)(char *, const char *, size_t) = strncat;
static size_t (*strxfrm_signature)(char *, const char *, size_t) = strxfrm;
static int (*wcsncmp_signature)(
    const wchar_t *, const wchar_t *, size_t) = wcsncmp;
static wchar_t *(*wcsncat_signature)(
    wchar_t *, const wchar_t *, size_t) = wcsncat;
static size_t (*wcsxfrm_signature)(
    wchar_t *, const wchar_t *, size_t) = wcsxfrm;

int main(void) {
    size_t above_32_bits = (size_t)4294967296ULL;
    size_t maximum = (size_t)-1;
    char narrow_a[8] = "A";
    char narrow_b[8] = "D";
    char narrow_transformed[8] = {'?', '?', '?', '?', '?', '?', '?', '?'};
    wchar_t wide_a[8] = L"G";
    wchar_t wide_b[8] = L"J";
    wchar_t wide_transformed[8] = {
        L'?', L'?', L'?', L'?', L'?', L'?', L'?', L'?'
    };

    if (above_32_bits == 0) return 0;

    if (strncmp_signature("a", "b", above_32_bits) >= 0) return 1;
    if (strncmp_signature("b", "a", maximum) <= 0) return 2;

    if (strncat_signature(narrow_a, "bc", above_32_bits) != narrow_a ||
        narrow_a[0] != 'A' || narrow_a[1] != 'b' ||
        narrow_a[2] != 'c' || narrow_a[3] != '\0') {
        return 3;
    }
    if (strncat_signature(narrow_b, "ef", maximum) != narrow_b ||
        narrow_b[0] != 'D' || narrow_b[1] != 'e' ||
        narrow_b[2] != 'f' || narrow_b[3] != '\0') {
        return 4;
    }
    if (strxfrm_signature(narrow_transformed, "mn", maximum) != 2 ||
        narrow_transformed[0] != 'm' || narrow_transformed[1] != 'n' ||
        narrow_transformed[2] != '\0') {
        return 5;
    }

    if (wcsncmp_signature(L"a", L"b", above_32_bits) >= 0) return 6;
    if (wcsncmp_signature(L"b", L"a", maximum) <= 0) return 7;

    if (wcsncat_signature(wide_a, L"hi", above_32_bits) != wide_a ||
        wide_a[0] != L'G' || wide_a[1] != L'h' ||
        wide_a[2] != L'i' || wide_a[3] != L'\0') {
        return 8;
    }
    if (wcsncat_signature(wide_b, L"kl", maximum) != wide_b ||
        wide_b[0] != L'J' || wide_b[1] != L'k' ||
        wide_b[2] != L'l' || wide_b[3] != L'\0') {
        return 9;
    }
    if (wcsxfrm_signature(wide_transformed, L"op", maximum) != 2 ||
        wide_transformed[0] != L'o' || wide_transformed[1] != L'p' ||
        wide_transformed[2] != L'\0') {
        return 10;
    }
    return 0;
}
