// wchar.h ASCII multibyte conversion runtime calls
// Expected: exit=0
#include <stddef.h>
#include <wchar.h>

int main(void) {
    wchar_t wc = 0;
    char out[4] = {0};
    if (btowc('A') != L'A') return 1;
    if (btowc(-1) != WEOF) return 2;
    if (wctob(L'Z') != 'Z') return 3;
    if (wctob(300) != -1) return 4;
    if (mbrtowc(&wc, "q", 1, 0) != 1 || wc != L'q') return 5;
    if (mbrtowc(&wc, "", 1, 0) != 0 || wc != 0) return 6;
    if (mbrtowc(&wc, "x", 0, 0) != (size_t)-2) return 7;
    if (wcrtomb(out, L'k', 0) != 1 || out[0] != 'k') return 8;
    {
        const char *src = "az";
        wchar_t wide[4] = {0};
        if (mbsrtowcs(wide, &src, 4, 0) != 2) return 9;
        if (src != 0 || wide[0] != L'a' || wide[1] != L'z' || wide[2] != 0) return 10;
    }
    {
        const wchar_t *src = L"by";
        char narrow[4] = {0};
        if (wcsrtombs(narrow, &src, 4, 0) != 2) return 11;
        if (src != 0 || narrow[0] != 'b' || narrow[1] != 'y' || narrow[2] != 0) return 12;
    }
    return 0;
}
