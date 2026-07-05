// Wasm standalone wchar.h wide input and swscanf stubs
// Expected: exit=0
#include <wchar.h>

int main(void) {
    FILE *stream = (FILE *)1;
    int a = 0;
    unsigned b = 0;
    wchar_t word[8];
    wchar_t ch = 0;

    if (swscanf(L" -12:34", L" %d:%u", &a, &b) != 2) return 1;
    if (a != -12 || b != 34u) return 2;

    if (swscanf(L"wide Z", L"%s %c", word, &ch) != 2) return 3;
    if (wcscmp(word, L"wide") != 0 || ch != L'Z') return 4;

    a = 99;
    b = 77;
    if (swscanf(L"x", L"%d:%u", &a, &b) != 0) return 5;
    if (a != 99 || b != 77) return 6;

    if (fgetwc(stream) != WEOF) return 7;
    if (getwc(stream) != WEOF) return 8;
    if (getwchar() != WEOF) return 9;
    if (ungetwc(L'A', stream) != WEOF) return 10;
    word[0] = L'k';
    word[1] = 0;
    if (fgetws(word, 8, stream) != 0) return 11;
    if (word[0] != L'k' || word[1] != 0) return 12;

    return 0;
}
