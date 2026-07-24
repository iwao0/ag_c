// ctype.h and wctype.h signatures, indirect calls, and domain boundaries.
// Expected: exit=0
#include <ctype.h>
#include <stdio.h>
#include <wctype.h>

typedef int (*narrow_predicate_t)(int);
typedef int (*wide_predicate_t)(wint_t);

static narrow_predicate_t narrow_predicates[] = {
    isalnum, isalpha, isblank, iscntrl, isdigit, isgraph,
    islower, isprint, ispunct, isspace, isupper, isxdigit,
};
static wide_predicate_t wide_predicates[] = {
    iswalnum, iswalpha, iswblank, iswcntrl, iswdigit, iswgraph,
    iswlower, iswprint, iswpunct, iswspace, iswupper, iswxdigit,
};

static int (*tolower_signature)(int) = tolower;
static int (*toupper_signature)(int) = toupper;
static wctype_t (*wctype_signature)(const char *) = wctype;
static int (*iswctype_signature)(wint_t, wctype_t) = iswctype;
static wint_t (*towlower_signature)(wint_t) = towlower;
static wint_t (*towupper_signature)(wint_t) = towupper;
static wctrans_t (*wctrans_signature)(const char *) = wctrans;
static wint_t (*towctrans_signature)(wint_t, wctrans_t) = towctrans;

static int matches_narrow(int index, int value, int expected) {
    return (narrow_predicates[index](value) != 0) == expected;
}

static int matches_wide(int index, wint_t value, int expected) {
    return (wide_predicates[index](value) != 0) == expected;
}

int main(void) {
    static const int narrow_true[] = {
        'A', 'A', ' ', '\n', '7', '!', 'z', ' ', '!',
        '\t', 'Z', 'f',
    };
    static const int narrow_false[] = {
        '!', '7', '\n', 'A', 'A', ' ', 'A', '\n', 'A',
        'A', 'a', 'g',
    };
    static const wint_t wide_true[] = {
        L'A', L'A', L' ', L'\n', L'7', L'!', L'z', L' ', L'!',
        L'\t', L'Z', L'f',
    };
    static const wint_t wide_false[] = {
        L'!', L'7', L'\n', L'A', L'A', L' ', L'A', L'\n', L'A',
        L'A', L'a', L'g',
    };
    static const char *properties[] = {
        "alnum", "alpha", "blank", "cntrl", "digit", "graph",
        "lower", "print", "punct", "space", "upper", "xdigit",
    };

    for (int i = 0; i < 12; ++i) {
        if (!matches_narrow(i, narrow_true[i], 1) ||
            !matches_narrow(i, narrow_false[i], 0)) return 1;
        if (narrow_predicates[i](EOF) != 0 ||
            narrow_predicates[i]((unsigned char)255) != 0) return 2;

        if (!matches_wide(i, wide_true[i], 1) ||
            !matches_wide(i, wide_false[i], 0)) return 3;
        if (wide_predicates[i](WEOF) != 0) return 4;

        wctype_t descriptor = wctype_signature(properties[i]);
        if (!descriptor) return 40 + i;
        if ((iswctype_signature(wide_true[i], descriptor) != 0) !=
                (wide_predicates[i](wide_true[i]) != 0) ||
            (iswctype_signature(wide_false[i], descriptor) != 0) !=
                (wide_predicates[i](wide_false[i]) != 0)) return 20 + i;
    }

    if (tolower_signature('A') != 'a' || tolower_signature('!') != '!' ||
        tolower_signature(EOF) != EOF ||
        tolower_signature((unsigned char)255) != (unsigned char)255) return 6;
    if (toupper_signature('a') != 'A' || toupper_signature('!') != '!' ||
        toupper_signature(EOF) != EOF ||
        toupper_signature((unsigned char)255) != (unsigned char)255) return 7;

    if (towlower_signature(L'A') != L'a' || towlower_signature(WEOF) != WEOF)
        return 8;
    if (towupper_signature(L'a') != L'A' || towupper_signature(WEOF) != WEOF)
        return 9;

    wctrans_t lower = wctrans_signature("tolower");
    wctrans_t upper = wctrans_signature("toupper");
    if (!lower || !upper || wctrans_signature("missing") != 0) return 10;
    if (towctrans_signature(L'Q', lower) != L'q' ||
        towctrans_signature(L'q', upper) != L'Q' ||
        towctrans_signature(WEOF, lower) != WEOF) return 11;
    if (wctype_signature("missing") != 0 ||
        iswctype_signature(L'A', (wctype_t)0) != 0) return 12;
    return 0;
}
