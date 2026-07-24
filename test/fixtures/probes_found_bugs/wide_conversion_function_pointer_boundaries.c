// wchar.h numeric and restartable conversion signatures through indirect calls.
// Expected: exit=0
#include <stddef.h>
#include <wchar.h>

static long (*wcstol_signature)(const wchar_t *, wchar_t **, int) = wcstol;
static unsigned long (*wcstoul_signature)(const wchar_t *, wchar_t **, int) = wcstoul;
static long long (*wcstoll_signature)(const wchar_t *, wchar_t **, int) = wcstoll;
static unsigned long long (*wcstoull_signature)(const wchar_t *, wchar_t **, int) = wcstoull;
static float (*wcstof_signature)(const wchar_t *, wchar_t **) = wcstof;
static double (*wcstod_signature)(const wchar_t *, wchar_t **) = wcstod;
static long double (*wcstold_signature)(const wchar_t *, wchar_t **) = wcstold;

static wint_t (*btowc_signature)(int) = btowc;
static int (*wctob_signature)(wint_t) = wctob;
static size_t (*mbrtowc_signature)(wchar_t *, const char *, size_t, mbstate_t *) = mbrtowc;
static size_t (*mbrlen_signature)(const char *, size_t, mbstate_t *) = mbrlen;
static int (*mbsinit_signature)(const mbstate_t *) = mbsinit;
static size_t (*wcrtomb_signature)(char *, wchar_t, mbstate_t *) = wcrtomb;
static size_t (*mbsrtowcs_signature)(
    wchar_t *, const char **, size_t, mbstate_t *) = mbsrtowcs;
static size_t (*wcsrtombs_signature)(
    char *, const wchar_t **, size_t, mbstate_t *) = wcsrtombs;

int main(void) {
    const wchar_t signed_value[] = L" -2aZ";
    const wchar_t unsigned_value[] = L"100000001x";
    const wchar_t float_value[] = L"-12.5!";
    const wchar_t no_value[] = L"  +x";
    wchar_t *end = 0;
    mbstate_t state = {0};
    wchar_t wc = 0;
    char narrow[8] = {0};
    wchar_t wide[8] = {0};

    if (wcstol_signature(signed_value, &end, 16) != -42L ||
        end != signed_value + 4) return 1;
    if (wcstoul_signature(unsigned_value, &end, 2) != 257UL ||
        end != unsigned_value + 9) return 2;
    if (wcstoll_signature(L"-4294967297z", &end, 10) != -4294967297LL ||
        *end != L'z') return 3;
    if (wcstoull_signature(L"4294967297z", &end, 10) != 4294967297ULL ||
        *end != L'z') return 4;
    if (wcstof_signature(float_value, &end) != -12.5f ||
        end != float_value + 5) return 5;
    if (wcstod_signature(float_value, &end) != -12.5 ||
        end != float_value + 5) return 6;
    if (wcstold_signature(float_value, &end) != -12.5L ||
        end != float_value + 5) return 7;
    if (wcstol_signature(no_value, &end, 10) != 0 || end != no_value) return 8;

    if (btowc_signature('A') != L'A' || btowc_signature(-1) != WEOF) return 9;
    if (wctob_signature(L'Z') != 'Z' || wctob_signature((wint_t)300) != -1)
        return 10;
    if (!mbsinit_signature(&state) || !mbsinit_signature(0)) return 11;
    if (mbrtowc_signature(&wc, "q", 1, &state) != 1 || wc != L'q') return 12;
    if (mbrtowc_signature(&wc, "", 1, &state) != 0 || wc != 0) return 13;
    if (mbrtowc_signature(&wc, "x", 0, &state) != (size_t)-2) return 14;
    if (mbrtowc_signature(&wc, 0, 0, &state) != 0) return 15;
    if (mbrlen_signature("A", 1, &state) != 1 ||
        mbrlen_signature("", 1, &state) != 0 ||
        mbrlen_signature("x", 0, &state) != (size_t)-2 ||
        mbrlen_signature(0, 0, &state) != 0) return 16;
    if (wcrtomb_signature(narrow, L'k', &state) != 1 || narrow[0] != 'k' ||
        wcrtomb_signature(0, L'Q', &state) != 1) return 17;

    {
        const char source[] = "abc";
        const char *query = source;
        if (mbsrtowcs_signature(0, &query, 0, &state) != 3 ||
            query != source) return 18;
        query = source;
        if (mbsrtowcs_signature(wide, &query, 2, &state) != 2 ||
            wide[0] != L'a' || wide[1] != L'b' || query != source + 2)
            return 19;
        if (mbsrtowcs_signature(wide + 2, &query, 6, &state) != 1 ||
            wide[2] != L'c' || wide[3] != 0 || query != 0) return 20;
    }

    {
        const wchar_t source[] = L"xyz";
        const wchar_t *query = source;
        if (wcsrtombs_signature(0, &query, 0, &state) != 3 ||
            query != source) return 21;
        query = source;
        if (wcsrtombs_signature(narrow, &query, 2, &state) != 2 ||
            narrow[0] != 'x' || narrow[1] != 'y' || query != source + 2)
            return 22;
        if (wcsrtombs_signature(narrow + 2, &query, 6, &state) != 1 ||
            narrow[2] != 'z' || narrow[3] != 0 || query != 0) return 23;
    }
    return 0;
}
