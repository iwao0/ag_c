// WAT standalone stdlib legacy multibyte conversion stubs
// Expected: exit=0
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    int wc = 0;
    char out[8] = {0};
    int wide[8] = {0};
    char narrow[8] = {0};
    int srcw[4] = {'o', 'k', 0, 0};

    if (mblen(0, 0) != 0) return 1;
    if (mblen("A", 1) != 1) return 2;
    if (mblen("", 1) != 0) return 3;
    if (mblen("B", 0) != -1) return 4;

    if (mbtowc(0, 0, 0) != 0) return 5;
    if (mbtowc(&wc, "C", 1) != 1 || wc != 'C') return 6;
    wc = 99;
    if (mbtowc(&wc, "", 1) != 0 || wc != 0) return 7;
    if (mbtowc(&wc, "D", 0) != -1) return 8;

    if (wctomb(0, 'E') != 0) return 9;
    if (wctomb(out, 'F') != 1 || out[0] != 'F') return 10;

    if (mbstowcs(0, 0, 4) != -1) return 11;
    if (mbstowcs(wide, "hi", 4) != 2) return 12;
    if (wide[0] != 'h' || wide[1] != 'i' || wide[2] != 0) return 13;

    if (wcstombs(0, 0, 4) != -1) return 14;
    if (wcstombs(narrow, srcw, sizeof(narrow)) != 2) return 15;
    if (narrow[0] != 'o' || narrow[1] != 'k' || narrow[2] != 0) return 16;

    return 0;
}
