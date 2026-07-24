// Standard multibyte declarations and single-byte locale boundaries.
// Expected: exit=0
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>

static int (*mblen_signature)(const char *, size_t) = mblen;
static int (*mbtowc_signature)(wchar_t *, const char *, size_t) = mbtowc;
static int (*wctomb_signature)(char *, wchar_t) = wctomb;
static size_t (*mbstowcs_signature)(wchar_t *, const char *, size_t) = mbstowcs;
static size_t (*wcstombs_signature)(char *, const wchar_t *, size_t) = wcstombs;

int main(void) {
    const char narrow_source[] = "abc";
    const wchar_t wide_source[] = L"xy";
    wchar_t wc = 0;
    wchar_t wide[5] = {99, 99, 99, 99, 99};
    char narrow[5] = {99, 99, 99, 99, 99};

    if (mblen_signature(0, 0) != 0) return 1;
    if (mblen_signature(narrow_source, sizeof(narrow_source)) != 1) return 2;
    if (mbtowc_signature(&wc, narrow_source, sizeof(narrow_source)) != 1 ||
        wc != L'a') return 3;

    if (mbstowcs_signature(0, narrow_source, 0) != 3) return 4;
    if (mbstowcs_signature(wide, narrow_source, 2) != 2) return 5;
    if (wide[0] != L'a' || wide[1] != L'b' || wide[2] != 99) return 6;
    if (mbstowcs_signature(wide, narrow_source, 5) != 3) return 7;
    if (wide[0] != L'a' || wide[1] != L'b' || wide[2] != L'c' ||
        wide[3] != L'\0') return 8;

    if (wcstombs_signature(0, wide_source, 0) != 2) return 9;
    if (wcstombs_signature(narrow, wide_source, 1) != 1) return 10;
    if (narrow[0] != 'x' || narrow[1] != 99) return 11;
    if (wcstombs_signature(narrow, wide_source, 5) != 2) return 12;
    if (narrow[0] != 'x' || narrow[1] != 'y' || narrow[2] != '\0') return 13;

    if (wctomb_signature(narrow, L'Z') != 1 || narrow[0] != 'Z') return 14;
    if (wctomb_signature(0, L'Z') != 0) return 15;
    return 0;
}
