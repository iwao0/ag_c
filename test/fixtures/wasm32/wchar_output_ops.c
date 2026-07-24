// Wasm standalone wchar.h output-only wide I/O stubs
// Expected: exit=0
#include <wchar.h>

int main(void) {
    FILE *stream = (FILE *)1;
    if (fputwc(L'A', stream) != L'A') return 1;
    if (putwc(L'B', stream) != L'B') return 2;
    if (putwchar(L'C') != L'C') return 3;
    if (fputws(L"wide", stream) != 4) return 4;
    if (fputwc(L'Z', 0) != WEOF) return 5;
    if (putwc(L'Z', 0) != WEOF) return 6;
    if (fputws(L"wide", 0) != WEOF) return 7;
    if (fwide(stream, 1) != 1) return 8;
    if (fwide(stream, -1) != 1) return 9;
    if (fwide(stream, 0) != 1) return 10;
    return 0;
}
