// wctype.h wide classification and transform runtime calls
// Expected: exit=0
#include <wctype.h>

int main(void) {
    if (!iswalnum(L'A') || !iswalnum(L'7') || iswalnum(L'!')) return 1;
    if (!iswalpha(L'Z') || iswalpha(L'4')) return 2;
    if (!iswblank(L' ') || !iswblank(L'\t') || iswblank(L'\n')) return 3;
    if (!iswcntrl(0) || !iswcntrl(127) || iswcntrl(L'a')) return 4;
    if (!iswdigit(L'9') || iswdigit(L'x')) return 5;
    if (!iswgraph(L'!') || iswgraph(L' ')) return 6;
    if (!iswlower(L'z') || iswlower(L'Z')) return 7;
    if (!iswprint(L' ') || iswprint(L'\n')) return 8;
    if (!iswpunct(L'?') || iswpunct(L'A')) return 9;
    if (!iswspace(L' ') || !iswspace(L'\n') || iswspace(L'k')) return 10;
    if (!iswupper(L'Q') || iswupper(L'q')) return 11;
    if (!iswxdigit(L'f') || !iswxdigit(L'F') || iswxdigit(L'g')) return 12;
    if (towlower(L'M') != L'm' || towlower(L'!') != L'!') return 13;
    if (towupper(L'm') != L'M' || towupper(L'!') != L'!') return 14;

    wctype_t digit = wctype("digit");
    wctype_t punct = wctype("punct");
    if (!digit || !iswctype(L'8', digit) || iswctype(L'x', digit)) return 15;
    if (!punct || !iswctype(L'!', punct) || iswctype(L'A', punct)) return 16;
    if (wctype("missing") != 0) return 17;

    wctrans_t lower = wctrans("tolower");
    wctrans_t upper = wctrans("toupper");
    if (!lower || !upper) return 18;
    if (towctrans(L'R', lower) != L'r') return 19;
    if (towctrans(L'r', upper) != L'R') return 20;
    if (towctrans(L'R', 0) != L'R') return 21;
    if (wctrans("missing") != 0) return 22;
    return 0;
}
