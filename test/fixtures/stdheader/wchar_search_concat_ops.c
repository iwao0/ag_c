// wchar.h wide search and bounded concat runtime calls
// Expected: exit=0
#include <wchar.h>

int main(void) {
    wchar_t buf[16] = L"ab";
    wchar_t xfrm[8];
    wchar_t small[4];
    wchar_t words[32] = L",alpha,,beta,gamma";
    wchar_t *save = 0;
    wchar_t *tok;
    if (wcsncat(buf, L"cdef", 2) != buf) return 1;
    if (wcscmp(buf, L"abcd") != 0) return 2;
    if (wcsncat(buf, L"XY", 0) != buf) return 3;
    if (wcscmp(buf, L"abcd") != 0) return 4;
    if (wcsstr(buf, L"bc") != buf + 1) return 5;
    if (wcsstr(buf, L"") != buf) return 6;
    if (wcsstr(buf, L"zz") != 0) return 7;
    if (wcscoll(L"abcd", buf) != 0) return 8;
    if (wcscoll(L"abce", buf) <= 0) return 9;
    if (wcsxfrm(xfrm, L"wide", 8) != 4) return 10;
    if (wcscmp(xfrm, L"wide") != 0) return 11;
    if (wcsxfrm(small, L"abcdef", 4) != 6) return 12;
    if (wcscmp(small, L"abc") != 0) return 13;
    if (wcsspn(L"abc123", L"cba") != 3) return 14;
    if (wcscspn(L"abc123", L"0123456789") != 3) return 15;
    if (wcspbrk(buf, L"cd") != buf + 2) return 16;
    if (wcspbrk(buf, L"xy") != 0) return 17;
    tok = wcstok(words, L",", &save);
    if (tok != words + 1 || wcscmp(tok, L"alpha") != 0) return 18;
    tok = wcstok(0, L",", &save);
    if (tok != words + 8 || wcscmp(tok, L"beta") != 0) return 19;
    tok = wcstok(0, L",", &save);
    if (tok != words + 13 || wcscmp(tok, L"gamma") != 0) return 20;
    if (wcstok(0, L",", &save) != 0) return 21;
    return 0;
}
