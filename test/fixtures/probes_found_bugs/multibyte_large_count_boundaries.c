/*
 * Multibyte conversion limits are size_t values.  SIZE_MAX remains a large
 * upper bound when the short ASCII input terminates before that limit.
 * Expected: exit=0
 */
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>

static int (*mblen_signature)(const char *, size_t) = mblen;
static int (*mbtowc_signature)(wchar_t *, const char *, size_t) = mbtowc;
static size_t (*mbstowcs_signature)(
    wchar_t *, const char *, size_t) = mbstowcs;
static size_t (*wcstombs_signature)(
    char *, const wchar_t *, size_t) = wcstombs;
static size_t (*mbrtowc_signature)(
    wchar_t *, const char *, size_t, mbstate_t *) = mbrtowc;
static size_t (*mbrlen_signature)(
    const char *, size_t, mbstate_t *) = mbrlen;
static size_t (*mbsrtowcs_signature)(
    wchar_t *, const char **, size_t, mbstate_t *) = mbsrtowcs;
static size_t (*wcsrtombs_signature)(
    char *, const wchar_t **, size_t, mbstate_t *) = wcsrtombs;

int main(void) {
    size_t maximum = (size_t)-1;
    wchar_t wc = 0;
    wchar_t wide[4] = {L'?', L'?', L'?', L'?'};
    char narrow[4] = {'?', '?', '?', '?'};
    mbstate_t state = {0};
    const char *narrow_source;
    const wchar_t *wide_source;

    if (mblen_signature("A", maximum) != 1) return 1;
    if (mbtowc_signature(&wc, "B", maximum) != 1 || wc != L'B') return 2;
    if (mbrtowc_signature(&wc, "C", maximum, &state) != 1 ||
        wc != L'C') {
        return 3;
    }
    if (mbrlen_signature("D", maximum, &state) != 1) return 4;

    if (mbstowcs_signature(wide, "ef", maximum) != 2 ||
        wide[0] != L'e' || wide[1] != L'f' || wide[2] != L'\0') {
        return 5;
    }
    if (wcstombs_signature(narrow, L"gh", maximum) != 2 ||
        narrow[0] != 'g' || narrow[1] != 'h' || narrow[2] != '\0') {
        return 6;
    }

    wide[0] = wide[1] = wide[2] = L'?';
    narrow_source = "ij";
    if (mbsrtowcs_signature(
            wide, &narrow_source, maximum, &state) != 2 ||
        narrow_source != 0 ||
        wide[0] != L'i' || wide[1] != L'j' || wide[2] != L'\0') {
        return 7;
    }

    narrow[0] = narrow[1] = narrow[2] = '?';
    wide_source = L"kl";
    if (wcsrtombs_signature(
            narrow, &wide_source, maximum, &state) != 2 ||
        wide_source != 0 ||
        narrow[0] != 'k' || narrow[1] != 'l' || narrow[2] != '\0') {
        return 8;
    }
    return 0;
}
