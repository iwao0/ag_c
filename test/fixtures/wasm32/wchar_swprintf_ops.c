// Wasm standalone wchar.h swprintf stub
// Expected: exit=0
#include <wchar.h>

int main(void) {
    wchar_t buf[32];
    int n = swprintf(buf, 32, L"%d-%u-%%", -12, 34u);
    if (n != 8) return 1;
    if (wcscmp(buf, L"-12-34-%") != 0) return 2;

    wchar_t padded[16];
    int p = swprintf(padded, 16, L"[%02d]", 7);
    if (p != 4) return 3;
    if (wcscmp(padded, L"[07]") != 0) return 4;

    wchar_t small[4];
    small[0] = L'x';
    small[1] = L'x';
    small[2] = L'x';
    small[3] = L'x';
    int t = swprintf(small, 4, L"%d", 12345);
    if (t != 5) return 5;
    if (small[0] != L'1' || small[1] != L'2' || small[2] != L'3' || small[3] != 0) return 6;

    wchar_t count_only[2];
    count_only[0] = L'q';
    count_only[1] = L'r';
    int z = swprintf(count_only, 0, L"%d-%d", 5, 6);
    if (z != 3) return 7;
    if (count_only[0] != L'q' || count_only[1] != L'r') return 8;
    return 0;
}
